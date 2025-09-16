#include <spn/allocation/shared_ptr.hpp>
#include <spn/allocation/heap.hpp>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_shared_ptr, LOG_LEVEL_INF);

ZTEST_SUITE(shared_ptr_suite, NULL, NULL, NULL, NULL, NULL);

/// Test payload structure
struct TestPayload {
    int a{}, b{};
    TestPayload() = default;
    TestPayload(int a_val, int b_val) : a(a_val), b(b_val) {}
};

/// Test basic shared pointer construction
ZTEST(shared_ptr_suite, test_shared_ptr_basic_construction) {
    using heap_t = spn::Heap<256>;
    auto heap = heap_t{};

    spn::SharedPtr<TestPayload, heap_t> ptr{heap};

    zassert_equal(ptr.use_count(), 1, "Initial use count should be 1");
    zassert_true(ptr.unique(), "Initial shared_ptr should be unique");
    zassert_not_null(ptr.get(), "Pointer should not be null");
}

/// Test reference counting behavior
ZTEST(shared_ptr_suite, test_shared_ptr_reference_counting) {
    using heap_t = spn::Heap<256>;
    auto heap = heap_t{};

    spn::SharedPtr<TestPayload, heap_t> ptr{heap};
    zassert_equal(ptr.use_count(), 1, "Initial use count should be 1");
    zassert_true(ptr.unique(), "Initially should be unique");

    {
        auto ptr2 = ptr; // copy constructor
        zassert_equal(ptr.use_count(), 2, "Use count should be 2 after copy");
        zassert_equal(ptr2.use_count(), 2, "Copy should have same use count");
        zassert_false(ptr.unique(), "Should not be unique when shared");
        zassert_false(ptr2.unique(), "Copy should not be unique when shared");
        zassert_equal(ptr.get(), ptr2.get(), "Both should point to same object");
    }

    zassert_equal(ptr.use_count(), 1, "Use count should be 1 after copy destruction");
    zassert_true(ptr.unique(), "Should be unique again after copy destruction");
}

/// Test copy semantics and shared data
ZTEST(shared_ptr_suite, test_shared_ptr_copy_semantics) {
    using heap_t = spn::Heap<256>;
    auto heap = heap_t{};

    spn::SharedPtr<TestPayload, heap_t> ptr1{heap};
    ptr1.get()->a = 42;
    ptr1.get()->b = 84;

    auto ptr2 = ptr1;
    zassert_equal(ptr2.get()->a, 42, "Copy should share same data");
    zassert_equal(ptr2.get()->b, 84, "Copy should share same data");

    // modify through one pointer
    ptr2.get()->a = 100;
    zassert_equal(ptr1.get()->a, 100, "Changes should be visible through original");

    auto ptr3 = ptr2;
    zassert_equal(ptr1.use_count(), 3, "Three pointers should share object");
    zassert_equal(ptr2.use_count(), 3, "All should report same use count");
    zassert_equal(ptr3.use_count(), 3, "All should report same use count");
}

/// Test destruction and cleanup
ZTEST(shared_ptr_suite, test_shared_ptr_destruction) {
    using heap_t = spn::Heap<256>;
    auto heap = heap_t{};

    TestPayload* raw_ptr = nullptr;

    {
        spn::SharedPtr<TestPayload, heap_t> ptr{heap};
        raw_ptr = ptr.get();
        zassert_not_null(raw_ptr, "Object should be allocated");

        {
            auto ptr2 = ptr;
            zassert_equal(ptr.use_count(), 2, "Should have 2 references");
        }

        zassert_equal(ptr.use_count(), 1, "Should have 1 reference after inner scope");
    }

    // after all shared_ptrs are destroyed, test that we can allocate new objects
    spn::SharedPtr<TestPayload, heap_t> new_ptr{heap};
    zassert_not_null(new_ptr.get(), "Should be able to allocate new object");
}

/// Test static control block usage
ZTEST(shared_ptr_suite, test_shared_ptr_static_control_block) {
    using heap_t = spn::Heap<256>;
    auto heap = heap_t{};

    static auto cb = spn::SharedPtr<TestPayload, heap_t>::make_control_block();
    zassert_equal(cb._refcount.load(), 1, "Static control block should start with refcount 1");

    {
        spn::SharedPtr<TestPayload, heap_t> ptr(&cb);
        zassert_equal(ptr.use_count(), 2, "Using static control block should increment refcount");
        zassert_equal(cb._refcount.load(), 2, "Control block refcount should be 2");
    }

    zassert_equal(cb._refcount.load(), 1, "Control block refcount should return to 1");
}

/// Test reuse or realloc functionality
ZTEST(shared_ptr_suite, test_shared_ptr_reuse_or_realloc) {
    using heap_t = spn::Heap<256>;
    auto heap = heap_t{};

    static auto cb = spn::SharedPtr<TestPayload, heap_t>::make_control_block();

    {
        auto ptr = spn::reuse_or_realloc<TestPayload>(&cb, heap, 1, 2);
        zassert_equal(cb._refcount.load(), 2, "Control block should have 2 references");
        zassert_equal(ptr.get()->a, 1, "Object should be initialized with first parameter");
        zassert_equal(ptr.get()->b, 2, "Object should be initialized with second parameter");

        auto ptr2 = spn::reuse_or_realloc<TestPayload>(&cb, heap, 3, 4);
        zassert_equal(cb._refcount.load(), 2, "Control block should still have 2 references");
        zassert_true(ptr2.unique(), "New allocation should be unique");
        zassert_equal(ptr2.get()->a, 3, "New object should have different values");
        zassert_equal(ptr2.get()->b, 4, "New object should have different values");

        // original pointer should still have original values
        zassert_equal(ptr.get()->a, 1, "Original object should be unchanged");
        zassert_equal(ptr.get()->b, 2, "Original object should be unchanged");
    }
}

/// Test multiple independent control blocks
ZTEST(shared_ptr_suite, test_shared_ptr_multiple_control_blocks) {
    using heap_t = spn::Heap<512>;
    auto heap = heap_t{};

    static auto cb1 = spn::SharedPtr<TestPayload, heap_t>::make_control_block();
    static auto cb2 = spn::SharedPtr<TestPayload, heap_t>::make_control_block();

    {
        spn::SharedPtr<TestPayload, heap_t> ptr1(&cb1);
        spn::SharedPtr<TestPayload, heap_t> ptr2(&cb2);

        zassert_equal(cb1._refcount.load(), 2, "First control block should have 2 references");
        zassert_equal(cb2._refcount.load(), 2, "Second control block should have 2 references");

        zassert_not_equal(ptr1.get(), ptr2.get(), "Different control blocks should manage different objects");

        ptr1.get()->a = 10;
        ptr2.get()->a = 20;

        zassert_equal(ptr1.get()->a, 10, "Objects should be independent");
        zassert_equal(ptr2.get()->a, 20, "Objects should be independent");
    }

    zassert_equal(cb1._refcount.load(), 1, "Control blocks should be back to initial state");
    zassert_equal(cb2._refcount.load(), 1, "Control blocks should be back to initial state");
}