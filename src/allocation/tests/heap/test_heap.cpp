#include <etl/array.h>
#include <spn/allocation/heap.hpp>
#include <spn/allocation/shared_ptr.hpp>
#include <spn/allocation/unique_ptr.hpp>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

LOG_MODULE_REGISTER(test_heap, LOG_LEVEL_INF);

ZTEST_SUITE(heap_suite, NULL, NULL, NULL, NULL, NULL);

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

} // namespace

ZTEST(heap_suite, test_heap_basic_allocation) {
    constexpr auto heap_size = 512;
    auto           heap      = spn::Heap<heap_size>{};

    void* ptr = nullptr;
    zassert_ok(heap.alloc(&ptr, 128), "must succeed");
    zassert_not_null(ptr, "must return valid pointer");
    zassert_true(heap.owns(ptr), "must own allocated pointer");

    zassert_ok(heap.release(ptr), "must release");
}

ZTEST(heap_suite, test_heap_invalid_arguments) {
    constexpr auto heap_size = 128;
    auto           heap      = spn::Heap<heap_size>{};

    zassert_equal(heap.capacity(), heap_size, "must match template parameter");

    zassert_equal(heap.alloc(nullptr, 8), -EINVAL, "must reject nullptr out");

    void* ptr = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    zassert_equal(heap.alloc(&ptr, 0), -EINVAL, "must reject zero bytes");
    zassert_is_null(ptr, "must null pointer on failure");

    ptr = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    zassert_equal(heap.alloc_aligned(&ptr, 0, 16), -EINVAL, "must reject zero alignment");
    zassert_is_null(ptr, "must null pointer on failure");

    ptr = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    zassert_equal(heap.alloc_aligned(&ptr, 3, 16), -EINVAL, "must reject non-power-of-two alignment");
    zassert_is_null(ptr, "must null pointer on failure");

    ptr = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    zassert_equal(heap.alloc(&ptr, heap_size * 2), -ENOMEM, "must fail when oversized");
    zassert_is_null(ptr, "must null pointer on failure");

    zassert_equal(heap.release(nullptr), -EINVAL, "must reject nullptr");

    // test foreign pointer release
    int stack_var = 0;
    zassert_equal(heap.release(&stack_var), -EINVAL, "must reject foreign pointer");

    // calloc parameter validation
    zassert_equal(heap.calloc(nullptr, 16, 4), -EINVAL, "must reject nullptr out");

    ptr = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    zassert_equal(heap.calloc(&ptr, 0, 16), -EINVAL, "must reject zero num");
    zassert_is_null(ptr, "must null pointer on failure");

    ptr = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    zassert_equal(heap.calloc(&ptr, 16, 0), -EINVAL, "must reject zero size");
    zassert_is_null(ptr, "must null pointer on failure");

    // alloc_aligned parameter validation
    zassert_equal(heap.alloc_aligned(nullptr, 16, 64), -EINVAL, "must reject nullptr out");

    // realloc parameter validation
    zassert_equal(heap.realloc(nullptr, 128), -EINVAL, "must reject nullptr out");

    // emplace parameter validation
    zassert_equal(heap.emplace<CountingObject>(nullptr, K_NO_WAIT, 42), -EINVAL, "must reject nullptr out");

    // destroy parameter validation
    zassert_equal(heap.destroy<CountingObject>(nullptr), -EINVAL, "must reject nullptr");

    CountingObject stack_obj{42};
    zassert_equal(heap.destroy(&stack_obj), -EINVAL, "must reject non-owned pointer");
}

ZTEST(heap_suite, test_heap_full_allocation) {
    constexpr auto heap_size     = 1024;
    constexpr auto overprovision = 128;
    constexpr auto block_size    = 64;

    auto heap = spn::Heap<heap_size + overprovision>{};

    etl::array<void*, (heap_size + overprovision) / block_size + 8> blocks{};
    size_t                                                          allocated_blocks = 0;

    for (auto& slot : blocks) {
        void* ptr = nullptr;
        int   rc  = heap.alloc(&ptr, block_size);
        if (rc != 0) {
            zassert_is_null(ptr, "must null pointer on failure");
            break;
        }
        slot = ptr;
        memset(ptr, 'A', block_size);
        ++allocated_blocks;
    }

    zassert_true(allocated_blocks > 0, "must allocate at least one block");

    void* extra = nullptr;
    zassert_not_equal(heap.alloc(&extra, block_size), 0, "must fail when exhausted");
    zassert_is_null(extra, "must null pointer on failure");

    for (auto ptr : blocks) {
        if (ptr != nullptr) {
            zassert_ok(heap.release(ptr), "must succeed");
        }
    }
}

ZTEST(heap_suite, test_heap_aligned_allocations) {
    constexpr auto heap_size = 512;
    auto           heap      = spn::Heap<heap_size>{};

    void* aligned_ptr = nullptr;
    zassert_ok(heap.alloc_aligned(&aligned_ptr, 32, 128), "must succeed");
    zassert_not_null(aligned_ptr, "must return valid pointer");
    zassert_equal(reinterpret_cast<uintptr_t>(aligned_ptr) & (32 - 1), 0, "must satisfy alignment");
    zassert_ok(heap.release(aligned_ptr), "must succeed");

    void* oversized = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    zassert_equal(heap.alloc_aligned(&oversized, 16, heap_size * 2), -ENOMEM, "must fail when oversized");
    zassert_is_null(oversized, "must null pointer on failure");
}

ZTEST(heap_suite, test_heap_partial_allocations) {
    constexpr auto heap_size = 1024;
    auto           heap      = spn::Heap<heap_size>{};

    // test partial allocations with generous headroom (quarter of capacity)
    void* half_ptr = nullptr;
    zassert_ok(heap.alloc(&half_ptr, heap_size / 4), "must succeed");
    zassert_not_null(half_ptr, "must return valid pointer");
    memset(half_ptr, 'B', heap_size / 4);
    zassert_ok(heap.release(half_ptr), "must succeed");

    // test eighth allocation
    void* quarter_ptr = nullptr;
    zassert_ok(heap.alloc(&quarter_ptr, heap_size / 8), "must succeed");
    zassert_not_null(quarter_ptr, "must return valid pointer");
    memset(quarter_ptr, 'C', heap_size / 8);
    zassert_ok(heap.release(quarter_ptr), "must succeed");
}

ZTEST(heap_suite, test_heap_calloc_semantics) {
    constexpr auto heap_size = 256;
    auto           heap      = spn::Heap<heap_size>{};

    void* ptr = nullptr;
    zassert_ok(heap.calloc(&ptr, 16, 4), "must succeed");
    zassert_not_null(ptr, "must return valid pointer");
    for (size_t i = 0; i < 64; ++i) {
        zassert_equal(static_cast<unsigned char*>(ptr)[i], 0, "must zero memory");
    }
    zassert_ok(heap.release(ptr), "must succeed");

    ptr = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    zassert_equal(heap.calloc(&ptr, SIZE_MAX, 2), -EOVERFLOW, "must detect overflow");
    zassert_is_null(ptr, "must null pointer on failure");
}

ZTEST(heap_suite, test_heap_memory_reuse) {
    constexpr auto heap_size = 512;
    auto           heap      = spn::Heap<heap_size>{};

    auto large_alloc = heap_size / 4;

    // allocate and release multiple times
    void* ptr1 = nullptr;
    zassert_ok(heap.alloc(&ptr1, large_alloc), "must succeed");
    auto first_address = ptr1;
    zassert_ok(heap.release(ptr1), "must succeed");

    void* ptr2 = nullptr;
    zassert_ok(heap.alloc(&ptr2, large_alloc), "must succeed");
    zassert_equal(ptr2, first_address, "must reuse memory");
    zassert_ok(heap.release(ptr2), "must succeed");

    // test with different sizes
    void* ptr3 = nullptr;
    zassert_ok(heap.alloc(&ptr3, 128), "must succeed");
    zassert_ok(heap.release(ptr3), "must succeed");

    void* ptr4 = nullptr;
    zassert_ok(heap.alloc(&ptr4, heap_size / 2), "must succeed");
    zassert_ok(heap.release(ptr4), "must succeed");
}

ZTEST(heap_suite, test_heap_realloc_semantics) {
    constexpr auto heap_size = 512;
    auto           heap      = spn::Heap<heap_size>{};

    void* ptr = nullptr;
    zassert_ok(heap.realloc(&ptr, 128), "must behave like alloc when ptr null");
    zassert_not_null(ptr, "must return valid pointer");

    memset(ptr, 0x1A, 128);

    zassert_ok(heap.realloc(&ptr, 256), "must grow");
    zassert_not_null(ptr, "must keep valid pointer");
    zassert_equal(static_cast<unsigned char*>(ptr)[0], 0x1A, "must preserve data");

    zassert_ok(heap.realloc(&ptr, 64), "must shrink");
    zassert_not_null(ptr, "must retain pointer");
    zassert_equal(static_cast<unsigned char*>(ptr)[0], 0x1A, "must preserve data");

    void* grow_fail = nullptr;
    zassert_ok(heap.alloc(&grow_fail, heap_size / 2), "must succeed");

    void* to_fail = ptr;
    zassert_equal(heap.realloc(&to_fail, heap_size), -ENOMEM, "must fail when oversized");
    zassert_equal(to_fail, ptr, "must leave pointer unchanged on failure");

    zassert_ok(heap.release(grow_fail), "must succeed");

    void* zero_free = ptr;
    zassert_ok(heap.realloc(&zero_free, 0), "must free when size zero");
    zassert_is_null(zero_free, "must null pointer");
    ptr = zero_free;
}

ZTEST(heap_suite, test_heap_emplace_destroy_make_unique) {
    constexpr auto heap_size = 256;
    auto           heap      = spn::Heap<heap_size>{};

    CountingObject::reset_counters();

    CountingObject* obj = nullptr;
    zassert_ok(heap.emplace<CountingObject>(&obj, K_NO_WAIT, 42), "must succeed");
    zassert_not_null(obj, "must return valid pointer");
    zassert_equal(obj->value, 42, "must forward arguments");
    zassert_equal(CountingObject::constructed, 1, "must construct once");

    zassert_ok(heap.destroy(obj), "must succeed");
    zassert_equal(CountingObject::destructed, 1, "must call destructor");

    auto ptr = heap.make_unique<CountingObject>(K_NO_WAIT, 7);
    zassert_true(ptr, "must return owning pointer");
    zassert_equal(ptr->value, 7, "must construct with value");
    zassert_equal(CountingObject::constructed, 2, "must construct twice");

    ptr.reset();
    zassert_equal(CountingObject::destructed, 2, "must destroy on reset");
}

ZTEST(heap_suite, test_heap_smart_ptr_contract) {
    spn::Heap<512> heap;
    CountingObject::reset_counters();

    {
        auto s = heap.make_shared<CountingObject>(heap, K_NO_WAIT, 42);
        auto u = heap.make_unique<CountingObject>(K_NO_WAIT, 99);
        zassert_equal(s->value, 42, "must construct with value");
        zassert_equal(u->value, 99, "must construct with value");
        zassert_equal(CountingObject::constructed, 2, "must construct both");
    }
    zassert_equal(CountingObject::destructed, 2, "must destroy both on scope exit");
}
