#include <spn/allocation/pool.hpp>
#include <spn/allocation/shared_ptr.hpp>
#include <spn/allocation/slab.hpp>
#include <spn/allocation/unique_ptr.hpp>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <cstddef>

LOG_MODULE_REGISTER(test_pool, LOG_LEVEL_INF);

ZTEST_SUITE(pool_suite, NULL, NULL, NULL, NULL, NULL);

namespace {

struct PoolEntry {
    int value = 0;
};

struct CountingObject {
    inline static int constructed = 0;
    inline static int destructed  = 0;

    CountingObject(int v = 0) : value(v) { ++constructed; }
    ~CountingObject() { ++destructed; }

    static void reset_counters() {
        constructed = 0;
        destructed  = 0;
    }

    int value{};
};

struct TestHarness {
    static inline int    acquire_calls    = 0;
    static inline int    release_calls    = 0;
    static inline size_t last_acquired_id = SIZE_MAX;
    static inline size_t last_released_id = SIZE_MAX;
    static inline int    last_value       = 0;

    static void reset() {
        acquire_calls    = 0;
        release_calls    = 0;
        last_acquired_id = SIZE_MAX;
        last_released_id = SIZE_MAX;
        last_value       = 0;
    }

    static void on_acquire(PoolEntry& entry, size_t id) {
        last_acquired_id = id;
        ++acquire_calls;
        entry.value = static_cast<int>(id + 100);
    }

    static void on_release(PoolEntry& entry, size_t id) {
        last_released_id = id;
        ++release_calls;
        last_value  = entry.value;
        entry.value = -1;
    }

    static void initialize(PoolEntry& entry, size_t id) { entry.value = static_cast<int>(id + 1); }
};

} // namespace

ZTEST(pool_suite, test_pool_basic_acquire_release) {
    spn::Pool<PoolEntry, 4> pool;

    PoolEntry* first = nullptr;
    zassert_ok(pool.acquire(reinterpret_cast<void**>(&first)), "must succeed");
    zassert_not_null(first, "must return valid pointer");

    auto slot = pool.slot_of(first);
    zassert_true(slot.has_value(), "must return slot index");
    zassert_equal(pool.used(), 1, "must count single allocation");
    zassert_equal(pool.try_at(*slot), first, "must return allocated pointer");

    first->value = 42;
    zassert_ok(pool.release(first), "must succeed");
    zassert_equal(pool.used(), 0, "must be empty after release");
    zassert_is_null(pool.try_at(*slot), "must return null for released slot");

    PoolEntry* reacquired = nullptr;
    zassert_ok(pool.acquire(reinterpret_cast<void**>(&reacquired)), "must succeed after release");
    zassert_not_null(reacquired, "must return valid pointer");
    auto reacquired_slot = pool.slot_of(reacquired);
    zassert_true(reacquired_slot.has_value(), "must have slot");
    zassert_equal(*reacquired_slot, *slot, "must reuse freed slot");

    zassert_ok(pool.release(reacquired), "must release reacquired object");
}

ZTEST(pool_suite, test_pool_capacity_and_exhaustion) {
    spn::Pool<PoolEntry, 3> pool;
    PoolEntry*              slots[3]{};

    for (size_t i = 0; i < 3; ++i) {
        zassert_ok(pool.acquire(reinterpret_cast<void**>(&slots[i])), "must succeed within capacity");
        zassert_not_null(slots[i], "must return valid pointer");
        for (size_t j = 0; j < i; ++j) {
            zassert_true(slots[i] != slots[j], "must return unique pointers");
        }
    }

    zassert_equal(pool.used(), 3, "must equal capacity when exhausted");

    PoolEntry* overflow = nullptr;
    zassert_equal(
        pool.acquire(reinterpret_cast<void**>(&overflow), K_NO_WAIT),
        -ETIMEDOUT,
        "must timeout with K_NO_WAIT"
    );
    zassert_is_null(overflow, "must set nullptr on timeout");

    PoolEntry* overflow2 = nullptr;
    zassert_equal(
        pool.acquire(reinterpret_cast<void**>(&overflow2), K_MSEC(1)),
        -ETIMEDOUT,
        "must timeout with K_MSEC"
    );
    zassert_is_null(overflow2, "must set nullptr on timeout");

    zassert_ok(pool.release(slots[1]), "must release middle slot");
    zassert_equal(pool.used(), 2, "must decrease after release");

    PoolEntry* reused = nullptr;
    zassert_ok(pool.acquire(reinterpret_cast<void**>(&reused)), "must succeed after release");
    zassert_not_null(reused, "must return valid pointer");
    zassert_true(reused == slots[1], "must recycle freed slot");

    zassert_ok(pool.release(slots[0]), "must release first slot");
    zassert_ok(pool.release(slots[2]), "must release last slot");
    zassert_ok(pool.release(reused), "must release recycled slot");
    zassert_equal(pool.used(), 0, "must be empty after all releases");
}

ZTEST(pool_suite, test_pool_release_error_cases) {
    spn::Pool<PoolEntry, 2> pool;

    zassert_equal(pool.acquire(nullptr, K_NO_WAIT), -EINVAL, "must reject nullptr output");

    zassert_equal(pool.release(static_cast<PoolEntry*>(nullptr)), -EINVAL, "must reject null pointer");

    PoolEntry external{};
    zassert_equal(pool.release(&external), -EINVAL, "must reject foreign pointer");

    PoolEntry* p = nullptr;
    zassert_ok(pool.acquire(reinterpret_cast<void**>(&p)), "must succeed");
    zassert_not_null(p, "must return valid pointer");

    // test mid-slot pointer rejection prevents corruption
    auto mid_slot = reinterpret_cast<PoolEntry*>(reinterpret_cast<unsigned char*>(p) + 1);
    zassert_false(pool.owns(mid_slot), "must not own misaligned pointer");
    zassert_false(pool.slot_of(mid_slot).has_value(), "must not map misaligned pointer");
    zassert_equal(pool.release(mid_slot), -EINVAL, "must reject misaligned pointer");

    zassert_equal(pool.release(5U), -EINVAL, "must reject out-of-range index");
    zassert_ok(pool.release(p), "must succeed");
    zassert_equal(pool.release(p), -EINVAL, "must reject double release");

    PoolEntry* next = nullptr;
    zassert_ok(pool.acquire(reinterpret_cast<void**>(&next)), "must succeed after errors");
    zassert_not_null(next, "must return valid pointer");
    auto next_slot = pool.slot_of(next);
    zassert_true(next_slot.has_value(), "must have slot");
    zassert_ok(pool.release(*next_slot), "must release by index");
    zassert_equal(pool.used(), 0, "must be empty");
}

ZTEST(pool_suite, test_pool_hooks_are_invoked) {
    using PoolType = spn::Pool<PoolEntry, 2>;
    PoolType pool;

    TestHarness::reset();
    pool.attach_on_acquire(PoolType::hook_f::create<&TestHarness::on_acquire>());
    pool.attach_on_release(PoolType::hook_f::create<&TestHarness::on_release>());

    PoolEntry* p = nullptr;
    zassert_ok(pool.acquire(reinterpret_cast<void**>(&p)), "must succeed");
    zassert_not_null(p, "must return valid pointer");
    auto slot = pool.slot_of(p);
    zassert_true(slot.has_value(), "must have slot");

    zassert_equal(TestHarness::acquire_calls, 1, "must call acquire hook once");
    zassert_equal(TestHarness::last_acquired_id, *slot, "must pass slot id to hook");
    zassert_equal(p->value, static_cast<int>(*slot + 100), "must apply hook mutation");

    zassert_ok(pool.release(p), "must succeed");
    zassert_equal(TestHarness::release_calls, 1, "must call release hook once");
    zassert_equal(TestHarness::last_released_id, *slot, "must pass slot id to hook");
    zassert_equal(TestHarness::last_value, static_cast<int>(*slot + 100), "must observe value before release");
}

ZTEST(pool_suite, test_pool_initialize_and_iteration) {
    using PoolType = spn::Pool<PoolEntry, 4>;
    PoolType pool;

    // test for_each_allocated on empty pool
    int call_count = 0;
    pool.for_each_allocated([&](PoolEntry&, size_t) { ++call_count; });
    zassert_equal(call_count, 0, "must not iterate empty pool");

    auto init_delegate = PoolType::hook_f::create<&TestHarness::initialize>();
    pool.initialize_each(init_delegate);

    bool       visited[PoolType::capacity()]{};
    PoolEntry* acquired[PoolType::capacity()]{};

    for (size_t i = 0; i < PoolType::capacity(); ++i) {
        zassert_ok(pool.acquire(reinterpret_cast<void**>(&acquired[i])), "must succeed");
        zassert_not_null(acquired[i], "must return valid pointer");
        auto slot = pool.slot_of(acquired[i]);
        zassert_true(slot.has_value(), "must have slot");
        visited[*slot] = true;
        zassert_equal(acquired[i]->value, static_cast<int>(*slot + 1), "must apply initialization");
        auto* try_ptr = pool.try_at(*slot);
        zassert_equal(try_ptr, acquired[i], "must return allocated pointer");
    }

    bool iterated[PoolType::capacity()]{};
    pool.for_each_allocated([&](PoolEntry& entry, size_t id) {
        iterated[id] = true;
        entry.value  = static_cast<int>(id * 2);
    });

    for (size_t i = 0; i < PoolType::capacity(); ++i) {
        zassert_true(visited[i], "must visit every slot");
        zassert_true(iterated[i], "must iterate all allocations");
    }

    for (size_t i = 0; i < PoolType::capacity(); ++i) {
        auto slot = pool.slot_of(acquired[i]);
        zassert_true(slot.has_value(), "must have valid slot");
        zassert_equal(acquired[i]->value, static_cast<int>(*slot * 2), "must apply iteration mutation");
        zassert_ok(pool.release(acquired[i]), "must succeed");
    }

    zassert_equal(pool.used(), 0, "must be empty after all releases");

    // test allocator/deleter with non-trivial type
    spn::Pool<CountingObject, 3> obj_pool;
    CountingObject::reset_counters();

    auto alloc = obj_pool.template allocator<CountingObject>();
    auto del   = obj_pool.template deleter<CountingObject>();

    void* raw = nullptr;
    zassert_ok(alloc.allocate(&raw, K_NO_WAIT), "must succeed");
    zassert_not_null(raw, "must return valid pointer");
    zassert_equal(CountingObject::destructed, 1, "must call destroy_at for non-trivial type");

    auto* obj = etl::construct_at(static_cast<CountingObject*>(raw), 123);
    zassert_equal(obj->value, 123, "must construct with value");
    zassert_equal(CountingObject::constructed, 1, "must count construction");

    del(obj);
    zassert_equal(obj_pool.used(), 0, "must return to pool");
}

ZTEST(pool_suite, test_pool_smart_ptr_contract) {
    spn::Pool<PoolEntry, 8>                pool;
    spn::Slab<spn::ControlBlockStorage, 8> control_slab;

    {
        auto s   = pool.make_shared(control_slab, K_NO_WAIT);
        auto u   = pool.make_unique(K_NO_WAIT);
        s->value = 42;
        u->value = 99;
        zassert_equal(s->value, 42, "must store value");
        zassert_equal(u->value, 99, "must store value");
        zassert_equal(pool.used(), 2, "must count both allocations");
    }
    zassert_equal(pool.used(), 0, "must release on destruction");

    auto reused = pool.make_shared(control_slab, K_NO_WAIT);
    zassert_true(reused->value == 42 || reused->value == 99, "must reuse pool object without destruction");

    // test factory failures when resources exhausted
    spn::Pool<PoolEntry, 2>                small_pool;
    spn::Slab<spn::ControlBlockStorage, 2> small_ctrl;

    auto u1 = small_pool.make_unique(K_NO_WAIT);
    auto u2 = small_pool.make_unique(K_NO_WAIT);
    zassert_true(u1, "must succeed within capacity");
    zassert_true(u2, "must succeed within capacity");

    auto u3 = small_pool.make_unique(K_NO_WAIT);
    zassert_false(u3, "must return empty when pool exhausted");

    u1.reset();
    u2.reset();
    auto s1 = small_pool.make_shared(small_ctrl, K_NO_WAIT);
    auto s2 = small_pool.make_shared(small_ctrl, K_NO_WAIT);
    zassert_true(static_cast<bool>(s1), "must succeed within capacity");
    zassert_true(static_cast<bool>(s2), "must succeed within capacity");

    auto s3 = small_pool.make_shared(small_ctrl, K_NO_WAIT);
    zassert_false(static_cast<bool>(s3), "must return empty when pool exhausted");

    s1.reset();
    s2.reset();
    spn::Pool<PoolEntry, 3> larger_pool;
    auto                    s4 = larger_pool.make_shared(small_ctrl, K_NO_WAIT);
    auto                    s5 = larger_pool.make_shared(small_ctrl, K_NO_WAIT);
    zassert_true(static_cast<bool>(s4), "must succeed within capacity");
    zassert_true(static_cast<bool>(s5), "must succeed within capacity");
    auto s6 = larger_pool.make_shared(small_ctrl, K_NO_WAIT);
    zassert_false(static_cast<bool>(s6), "must return empty when control storage exhausted");
}
