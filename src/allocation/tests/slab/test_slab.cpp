#include <etl/array.h>
#include <spn/allocation/shared_ptr.hpp>
#include <spn/allocation/slab.hpp>
#include <spn/allocation/unique_ptr.hpp>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <cstddef>
#include <cstdint>

LOG_MODULE_REGISTER(test_slab, LOG_LEVEL_INF);

ZTEST_SUITE(slab_suite, NULL, NULL, NULL, NULL, NULL);

namespace {

struct CountingObject {
    inline static int constructed = 0;
    inline static int destructed  = 0;

    explicit CountingObject(int v) : value(v) { ++constructed; }
    ~CountingObject() { ++destructed; }

    static void reset_counters() {
        constructed = 0;
        destructed  = 0;
    }

    int value{};
};

struct NonAlignedStruct {
    int32_t a, b, c;
};

} // namespace

ZTEST(slab_suite, test_slab_basic_allocation) {
    constexpr auto num_blocks = 5;
    using value_t             = uint8_t[32];
    auto slab                 = spn::Slab<value_t, num_blocks>{};

    value_t* ptr = nullptr;
    zassert_ok(slab.alloc(reinterpret_cast<void**>(&ptr)), "must allocate");
    zassert_not_null(ptr, "must return valid pointer");
    zassert_ok(slab.release(ptr), "must release");
}

ZTEST(slab_suite, test_slab_emplace) {
    auto slab = spn::Slab<CountingObject, 8>{};
    CountingObject::reset_counters();

    CountingObject* obj = nullptr;
    zassert_ok(slab.emplace(reinterpret_cast<void**>(&obj), K_NO_WAIT, 42), "must emplace object");
    zassert_not_null(obj, "must return valid pointer");
    zassert_equal(obj->value, 42, "must construct with correct value");
    zassert_equal(CountingObject::constructed, 1, "must call constructor");

    zassert_ok(slab.destroy(obj), "must destroy emplaced object");
    zassert_equal(CountingObject::destructed, 1, "must call destructor");

    zassert_equal(slab.emplace(nullptr, K_NO_WAIT, 99), -EINVAL, "must reject nullptr");
}

ZTEST(slab_suite, test_slab_destroy) {
    auto slab = spn::Slab<int, 4>{};
    zassert_equal(slab.destroy(nullptr), -EINVAL, "must reject nullptr");

    // test trivially destructible type behaves correctly
    int* ptr = nullptr;
    zassert_ok(slab.emplace(reinterpret_cast<void**>(&ptr), K_NO_WAIT, 123), "must emplace int");
    zassert_equal(*ptr, 123, "must pass constructor argument");
    zassert_ok(slab.destroy(ptr), "must handle trivially destructible type");
}

ZTEST(slab_suite, test_slab_capacity_tracking) {
    constexpr auto num_blocks = 5;
    auto           slab       = spn::Slab<uint32_t, num_blocks>{};

    zassert_equal(slab.capacity(), num_blocks, "must report correct capacity");
    zassert_equal(slab.free(), num_blocks, "must start with all free");
    zassert_equal(slab.allocated(), 0, "must start with none allocated");

    uint32_t* ptr1 = nullptr;
    uint32_t* ptr2 = nullptr;
    zassert_ok(slab.alloc(reinterpret_cast<void**>(&ptr1)), "must allocate first");
    zassert_ok(slab.alloc(reinterpret_cast<void**>(&ptr2)), "must allocate second");

    zassert_equal(slab.free(), num_blocks - 2, "must track free correctly");
    zassert_equal(slab.allocated(), 2, "must track allocated correctly");
    zassert_equal(slab.free() + slab.allocated(), slab.capacity(), "must satisfy invariant");

    zassert_ok(slab.release(ptr1), "must release first");
    zassert_equal(slab.free(), num_blocks - 1, "must update free count");
    zassert_equal(slab.allocated(), 1, "must update allocated count");

    zassert_ok(slab.release(ptr2), "must release second");
    zassert_equal(slab.free(), num_blocks, "must return to all free");
    zassert_equal(slab.allocated(), 0, "must return to none allocated");
}

ZTEST(slab_suite, test_slab_full_allocation) {
    constexpr auto num_blocks = 10;
    using value_t             = uint8_t[32];
    auto slab                 = spn::Slab<value_t, num_blocks>{};

    etl::array<value_t*, num_blocks> ref_store{};

    for (int i = 0; i < num_blocks; i++) {
        zassert_ok(slab.alloc(reinterpret_cast<void**>(&ref_store[i])), "must allocate block %d", i);
        zassert_not_null(ref_store[i], "must return valid pointer");
    }

    // overflow allocation should fail
    value_t* overflow_ptr = nullptr;
    zassert_not_equal(slab.alloc(reinterpret_cast<void**>(&overflow_ptr)), 0, "must fail when full");
    zassert_is_null(overflow_ptr, "must set pointer to null on failure");

    for (int i = 0; i < num_blocks; i++) {
        zassert_ok(slab.release(ref_store[i]), "must release block %d", i);
    }
}

ZTEST(slab_suite, test_slab_reuse_after_release) {
    constexpr auto num_blocks = 4;
    using value_t             = uint8_t[128];
    auto slab                 = spn::Slab<value_t, num_blocks>{};

    etl::array<value_t*, num_blocks> ptrs{};

    for (int i = 0; i < num_blocks; i++) {
        zassert_ok(slab.alloc(reinterpret_cast<void**>(&ptrs[i])), "allocation %d must succeed", i);
    }

    value_t* extra = nullptr;
    zassert_not_equal(slab.alloc(reinterpret_cast<void**>(&extra)), 0, "must fail when full");

    for (int i = 0; i < num_blocks; i++) {
        zassert_ok(slab.release(ptrs[i]), "release %d must succeed", i);
    }

    // verify blocks can be reused after full cycle
    value_t* reuse_ptr = nullptr;
    zassert_ok(slab.alloc(reinterpret_cast<void**>(&reuse_ptr)), "must reuse after release");
    zassert_ok(slab.release(reuse_ptr), "cleanup must succeed");
}

ZTEST(slab_suite, test_slab_interleaved_operations) {
    constexpr auto num_blocks = 6;
    using value_t             = uint8_t[48];
    auto slab                 = spn::Slab<value_t, num_blocks>{};

    value_t* ptr1 = nullptr;
    value_t* ptr2 = nullptr;
    value_t* ptr3 = nullptr;

    zassert_ok(slab.alloc(reinterpret_cast<void**>(&ptr1)), "must allocate first block");
    zassert_ok(slab.alloc(reinterpret_cast<void**>(&ptr2)), "must allocate second block");
    zassert_ok(slab.alloc(reinterpret_cast<void**>(&ptr3)), "must allocate third block");

    zassert_ok(slab.release(ptr2), "must release middle block");

    // verify freed slot can be reused
    value_t* ptr4 = nullptr;
    zassert_ok(slab.alloc(reinterpret_cast<void**>(&ptr4)), "must allocate using freed slot");

    zassert_ok(slab.release(ptr1), "must release first block");
    zassert_ok(slab.release(ptr3), "must release third block");
    zassert_ok(slab.release(ptr4), "must release fourth block");
}

ZTEST(slab_suite, test_slab_release_error_cases) {
    constexpr auto num_blocks = 4;
    using value_t             = uint8_t[32];
    auto slab                 = spn::Slab<value_t, num_blocks>{};

    zassert_equal(slab.alloc(nullptr), -EINVAL, "must reject null output pointer");
    zassert_equal(slab.release(nullptr), -EINVAL, "must reject null pointer");

    value_t external_block;
    zassert_equal(slab.release(&external_block), -EINVAL, "must reject pointer not owned by slab");

    value_t* ptr = nullptr;
    zassert_ok(slab.alloc(reinterpret_cast<void**>(&ptr)), "must allocate");

    // mid-chunk pointer rejection prevents corruption
    auto mid_chunk = reinterpret_cast<void*>(reinterpret_cast<unsigned char*>(ptr) + 1);
    zassert_false(slab.owns(mid_chunk), "must not own misaligned pointer");
    zassert_equal(slab.release(mid_chunk), -EINVAL, "must reject misaligned pointer");

    zassert_ok(slab.release(ptr), "must release valid pointer");
}

ZTEST(slab_suite, test_slab_alignment) {
    // highly-aligned type exceeds sizeof(void*)
    struct alignas(32) HighlyAligned {
        uint64_t data[4];
    };

    auto slab = spn::Slab<HighlyAligned, 4>{};

    HighlyAligned* ptr1 = nullptr;
    HighlyAligned* ptr2 = nullptr;

    zassert_ok(slab.alloc(reinterpret_cast<void**>(&ptr1)), "must allocate first block");
    zassert_equal(reinterpret_cast<uintptr_t>(ptr1) % 32, 0, "must be 32-byte aligned");

    zassert_ok(slab.alloc(reinterpret_cast<void**>(&ptr2)), "must allocate second block");
    zassert_equal(reinterpret_cast<uintptr_t>(ptr2) % 32, 0, "must be 32-byte aligned");

    // owns() must respect chunk alignment
    zassert_true(slab.owns(ptr1), "must own chunk-aligned pointer");
    auto misaligned = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptr1) + 8);
    zassert_false(slab.owns(misaligned), "must not own misaligned pointer");

    zassert_ok(slab.release(ptr1), "must release first block");
    zassert_ok(slab.release(ptr2), "must release second block");
}

ZTEST(slab_suite, test_slab_smart_ptr_contract) {
    spn::Slab<CountingObject, 16>           slab;
    spn::Slab<spn::ControlBlockStorage, 16> control_slab;
    CountingObject::reset_counters();

    {
        auto s = slab.make_shared(control_slab, K_NO_WAIT, 42);
        auto u = slab.make_unique(K_NO_WAIT, 99);
        zassert_equal(s->value, 42, "must store initialized value");
        zassert_equal(u->value, 99, "must store initialized value");
        zassert_equal(CountingObject::constructed, 2, "must construct both objects");
    }
    zassert_equal(CountingObject::destructed, 2, "must destroy both objects");
}

ZTEST(slab_suite, test_slab_non_aligned_type) {
    auto slab = spn::Slab<NonAlignedStruct, 4>{};

    NonAlignedStruct* ptr = nullptr;
    zassert_ok(slab.alloc(reinterpret_cast<void**>(&ptr)), "must allocate non-aligned type");
    zassert_true(slab.owns(ptr), "must own allocated pointer");
    zassert_ok(slab.release(ptr), "must release");
}