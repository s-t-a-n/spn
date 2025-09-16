#include <spn/allocation/heap.hpp>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_heap, LOG_LEVEL_INF);

ZTEST_SUITE(heap_suite, NULL, NULL, NULL, NULL, NULL);

/// Test basic heap allocation and deallocation
ZTEST(heap_suite, test_heap_basic_allocation) {
    constexpr auto heap_size = 512;
    auto heap = spn::Heap<heap_size>{};

    void* ptr = nullptr;
    zassert_equal(heap.alloc(&ptr, 128), 0, "Basic allocation should succeed");
    zassert_not_null(ptr, "Allocated pointer should not be null");

    heap.release(ptr);
}

/// Test allocating full heap capacity
ZTEST(heap_suite, test_heap_full_allocation) {
    constexpr auto heap_size = 1024;
    constexpr auto overprovision = 128;
    auto heap = spn::Heap<heap_size + overprovision>{};

    // allocate full heap capacity
    void* ptr = nullptr;
    zassert_equal(heap.alloc(&ptr, heap_size), 0, "Full allocation should succeed");
    zassert_not_null(ptr, "Allocated pointer should not be null");

    // fill allocated memory
    memset(ptr, 'A', heap_size);

    // try to allocate more - should fail
    void* ptr2 = nullptr;
    zassert_not_equal(heap.alloc(&ptr2, heap_size), 0, "Second full allocation should fail");
    zassert_not_equal(heap.alloc(&ptr2, overprovision), 0, "Overprovision allocation should fail");

    heap.release(ptr);
}

/// Test partial heap allocations
ZTEST(heap_suite, test_heap_partial_allocations) {
    constexpr auto heap_size = 1024;
    auto heap = spn::Heap<heap_size>{};

    // test half allocation
    void* half_ptr = nullptr;
    zassert_equal(heap.alloc(&half_ptr, heap_size / 2), 0, "Half allocation should succeed");
    zassert_not_null(half_ptr, "Half allocated pointer should not be null");
    memset(half_ptr, 'B', heap_size / 2);
    heap.release(half_ptr);

    // test quarter allocation
    void* quarter_ptr = nullptr;
    zassert_equal(heap.alloc(&quarter_ptr, heap_size / 4), 0, "Quarter allocation should succeed");
    zassert_not_null(quarter_ptr, "Quarter allocated pointer should not be null");
    memset(quarter_ptr, 'C', heap_size / 4);
    heap.release(quarter_ptr);
}

/// Test allocation failure when heap is full
ZTEST(heap_suite, test_heap_allocation_failure) {
    constexpr auto heap_size = 256;
    constexpr auto alloc_size = heap_size - 64; // Leave room for heap overhead
    auto heap = spn::Heap<heap_size>{};

    // allocate most of the heap
    void* ptr1 = nullptr;
    zassert_equal(heap.alloc(&ptr1, alloc_size), 0, "Large allocation should succeed");

    // try to allocate when heap is nearly full
    void* ptr2 = nullptr;
    zassert_not_equal(heap.alloc(&ptr2, 64), 0, "Allocation when nearly full should fail");
    zassert_is_null(ptr2, "Failed allocation should return null pointer");

    heap.release(ptr1);
}


/// Test memory reuse after deallocation
ZTEST(heap_suite, test_heap_memory_reuse) {
    constexpr auto heap_size = 512;
    constexpr auto large_alloc = 256;
    constexpr auto max_alloc = heap_size - 64; // Account for heap overhead
    auto heap = spn::Heap<heap_size>{};

    // allocate and release multiple times
    void* ptr1 = nullptr;
    zassert_equal(heap.alloc(&ptr1, large_alloc), 0, "First allocation should succeed");
    auto first_address = ptr1;
    heap.release(ptr1);

    void* ptr2 = nullptr;
    zassert_equal(heap.alloc(&ptr2, large_alloc), 0, "Second allocation should succeed");
    zassert_equal(ptr2, first_address, "Memory should be reused");
    heap.release(ptr2);

    // test with different sizes
    void* ptr3 = nullptr;
    zassert_equal(heap.alloc(&ptr3, 128), 0, "Smaller allocation should succeed");
    heap.release(ptr3);

    void* ptr4 = nullptr;
    zassert_equal(heap.alloc(&ptr4, max_alloc), 0, "Larger allocation should succeed");
    heap.release(ptr4);
}