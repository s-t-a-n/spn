#include <spn/allocation/heap.hpp>
#include <spn/allocation/shared_ptr.hpp>
#include <spn/allocation/slab.hpp>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_shared_ptr, LOG_LEVEL_INF);

ZTEST_SUITE(shared_ptr_suite, NULL, NULL, NULL, NULL, NULL);

struct TestPayload {
    int a{}, b{};
    TestPayload() = default;
    TestPayload(int a_val, int b_val) : a(a_val), b(b_val) {}
};

struct CountingPayload {
    static inline int destroyed = 0;
    CountingPayload()           = default;
    ~CountingPayload() { ++destroyed; }
};

ZTEST(shared_ptr_suite, test_shared_ptr_basic_construction) {
    spn::Heap<256> heap;

    // default construction
    spn::shared_ptr<TestPayload> ptr1;
    zassert_false(ptr1, "default must be null");
    zassert_equal(ptr1.get(), nullptr, "must return nullptr");
    zassert_equal(ptr1.use_count(), 0, "must have zero refcount");

    // heap construction
    auto ptr2 = heap.make_shared<TestPayload>(heap, K_NO_WAIT, 1, 2);
    zassert_true(bool(ptr2), "must succeed");
    zassert_not_null(ptr2.get(), "must return valid pointer");
    zassert_equal(ptr2.use_count(), 1, "must have single owner");
    zassert_equal(ptr2->a, 1, "must match constructed value");
    zassert_equal(ptr2->b, 2, "must match constructed value");

    // dereference operator
    (*ptr2).a = 99;
    zassert_equal(ptr2->a, 99, "must allow mutation via operator*");
}

ZTEST(shared_ptr_suite, test_shared_ptr_reference_counting) {
    spn::Heap<256> heap;

    auto ptr = heap.make_shared<TestPayload>(heap, K_NO_WAIT, 42, 84);
    zassert_equal(ptr.use_count(), 1, "must have single owner");
    zassert_true(ptr.unique(), "must be unique");

    {
        auto ptr2 = ptr; // copy constructor
        zassert_equal(ptr.use_count(), 2, "must increment refcount on copy");
        zassert_equal(ptr2.use_count(), 2, "must share same refcount");
        zassert_false(ptr.unique(), "must not be unique when shared");
        zassert_false(ptr2.unique(), "must not be unique when shared");
        zassert_equal(ptr.get(), ptr2.get(), "must point to same object");
    }

    zassert_equal(ptr.use_count(), 1, "must decrement refcount on destruction");
    zassert_true(ptr.unique(), "must become unique after copy destroyed");
}

ZTEST(shared_ptr_suite, test_shared_ptr_move_semantics) {
    spn::Heap<256> heap;

    auto         ptr1 = heap.make_shared<TestPayload>(heap, K_NO_WAIT, 10, 20);
    TestPayload* raw  = ptr1.get();

    auto ptr2 = etl::move(ptr1); // move constructor
    zassert_false(ptr1, "must leave source null");
    zassert_equal(ptr1.get(), nullptr, "must return nullptr from source");
    zassert_true(bool(ptr2), "must transfer to target");
    zassert_equal(ptr2.get(), raw, "must preserve pointer value");
    zassert_equal(ptr2.use_count(), 1, "must not change refcount");

    auto ptr3 = heap.make_shared<TestPayload>(heap, K_NO_WAIT, 30, 40);
    ptr3      = etl::move(ptr2); // move assignment
    zassert_false(ptr2, "must leave source null");
    zassert_equal(ptr3.get(), raw, "must transfer ownership");
}

ZTEST(shared_ptr_suite, test_shared_ptr_nullptr_assignment) {
    spn::Heap<256> heap;

    auto ptr = heap.make_shared<TestPayload>(heap, K_NO_WAIT, 1, 2);
    zassert_true(bool(ptr), "must succeed");

    ptr = nullptr;
    zassert_false(ptr, "must clear on nullptr assignment");
    zassert_equal(ptr.get(), nullptr, "must return nullptr");
    zassert_equal(ptr.use_count(), 0, "must have zero refcount");
}

ZTEST(shared_ptr_suite, test_shared_ptr_reset) {
    spn::Heap<256> heap;

    auto ptr = heap.make_shared<TestPayload>(heap, K_NO_WAIT, 5, 10);
    zassert_true(bool(ptr), "must succeed");

    ptr.reset();
    zassert_false(ptr, "must clear on reset");
    zassert_equal(ptr.use_count(), 0, "must have zero refcount");

    ptr = heap.make_shared<TestPayload>(heap, K_NO_WAIT, 15, 20);
    zassert_true(bool(ptr), "must succeed");
    zassert_equal(ptr->a, 15, "must have new values");
    zassert_equal(ptr->b, 20, "must have new values");

    ptr.reset(nullptr);
    zassert_false(ptr, "must clear on reset(nullptr)");
}

ZTEST(shared_ptr_suite, test_shared_ptr_heap_deleter_destroys) {
    spn::Heap<256> heap;
    CountingPayload::destroyed = 0;

    {
        auto ptr = heap.make_shared<CountingPayload>(heap, K_NO_WAIT);
        zassert_true(bool(ptr), "must succeed");
        zassert_equal(ptr.use_count(), 1, "must have single owner");
    }

    zassert_equal(CountingPayload::destroyed, 1, "must call destructor");

    struct RecordingHeap : spn::Heap<256> {
        int alloc_aligned_calls = 0;
        int release_calls       = 0;

        RecordingHeap() = default;

        int alloc_aligned(void** out, size_t alignment, size_t bytes, k_timeout_t timeout = K_NO_WAIT) override {
            ++alloc_aligned_calls;
            return spn::Heap<256>::alloc_aligned(out, alignment, bytes, timeout);
        }

        int release(void* ptr) override {
            ++release_calls;
            return spn::Heap<256>::release(ptr);
        }
    } object_heap, control_heap;

    const int object_alloc_before  = object_heap.alloc_aligned_calls;
    const int control_alloc_before = control_heap.alloc_aligned_calls;

    auto ptr = object_heap.make_shared<TestPayload>(control_heap, K_NO_WAIT, 7, 9);
    zassert_true(bool(ptr), "must succeed");
    zassert_equal(object_heap.alloc_aligned_calls, object_alloc_before + 1, "must allocate object from object heap");
    zassert_equal(
        control_heap.alloc_aligned_calls,
        control_alloc_before + 1,
        "must allocate control from control heap"
    );

    ptr.reset();
    zassert_equal(object_heap.release_calls, 1, "must release object through object heap");
    zassert_equal(control_heap.release_calls, 1, "must release control through control heap");

    spn::Slab<spn::ControlBlockStorage, 4> control_slab;

    auto slab_ptr = object_heap.make_shared<TestPayload>(control_slab, K_NO_WAIT, 11, 13);
    zassert_true(bool(slab_ptr), "must succeed");
    zassert_equal(control_slab.allocated(), 1U, "must allocate from slab");

    slab_ptr.reset();
    zassert_equal(control_slab.allocated(), 0U, "must free slab slot");
}

ZTEST(shared_ptr_suite, test_shared_ptr_comparison) {
    spn::Heap<256> heap;

    auto                         ptr1 = heap.make_shared<TestPayload>(heap, K_NO_WAIT, 1, 2);
    auto                         ptr2 = ptr1;
    auto                         ptr3 = heap.make_shared<TestPayload>(heap, K_NO_WAIT, 3, 4);
    spn::shared_ptr<TestPayload> null_ptr;

    // equality
    zassert_true(ptr1 == ptr2, "must compare equal when same");
    zassert_false(ptr1 == ptr3, "must not compare equal when different");
    zassert_true(null_ptr == nullptr, "must equal nullptr when null");
    zassert_true(nullptr == null_ptr, "must equal nullptr when null");
    zassert_false(ptr1 == nullptr, "must not equal nullptr when non-null");

    // inequality
    zassert_false(ptr1 != ptr2, "must not be unequal when same");
    zassert_true(ptr1 != ptr3, "must be unequal when different");
    zassert_false(null_ptr != nullptr, "must not differ from nullptr when null");
    zassert_true(ptr1 != nullptr, "must differ from nullptr when non-null");
}

ZTEST(shared_ptr_suite, test_shared_ptr_swap) {
    spn::Heap<256> heap;

    auto ptr1 = heap.make_shared<TestPayload>(heap, K_NO_WAIT, 11, 22);
    auto ptr2 = heap.make_shared<TestPayload>(heap, K_NO_WAIT, 33, 44);

    TestPayload* raw1 = ptr1.get();
    TestPayload* raw2 = ptr2.get();

    ptr1.swap(ptr2);

    zassert_equal(ptr1.get(), raw2, "must swap ptr1 to ptr2 object");
    zassert_equal(ptr2.get(), raw1, "must swap ptr2 to ptr1 object");
    zassert_equal(ptr1->a, 33, "must have swapped values");
    zassert_equal(ptr2->a, 11, "must have swapped values");
}

ZTEST(shared_ptr_suite, test_shared_ptr_const_conversion) {
    spn::Heap<256> heap;
    auto           ptr = heap.make_shared<TestPayload>(heap, K_NO_WAIT, 42, 84);

    spn::shared_ptr<const TestPayload> const_ptr = ptr;
    zassert_equal(const_ptr.get(), ptr.get(), "must point to same object");
    zassert_equal(const_ptr.use_count(), 2, "must share ownership");

    spn::shared_ptr<const TestPayload> moved_const = etl::move(ptr);
    zassert_false(ptr, "must leave source null");
}

ZTEST(shared_ptr_suite, test_shared_ptr_const_type_ownership) {
    spn::Heap<256> heap;

    auto const_ptr = heap.make_shared<const TestPayload>(heap, K_NO_WAIT, 10, 20);
    zassert_true(bool(const_ptr), "must succeed");
    zassert_equal(const_ptr->a, 10, "must access const members");
    zassert_equal(const_ptr->b, 20, "must access const members");
    zassert_equal(const_ptr.use_count(), 1, "must have single owner");
}

ZTEST(shared_ptr_suite, test_shared_ptr_base_conversion) {
    struct Base {
        int x{};
        virtual ~Base() = default;
    };
    struct Derived : Base {
        int y{};
    };

    spn::Heap<512> heap;
    auto           derived = heap.make_shared<Derived>(heap, K_NO_WAIT);
    derived->x             = 10;
    derived->y             = 20;

    spn::shared_ptr<Base> base = derived;
    zassert_equal(base.get(), derived.get(), "must point to same object");
    zassert_equal(base->x, 10, "must access base members");
    zassert_equal(base.use_count(), 2, "must share ownership");
}

ZTEST(shared_ptr_suite, test_shared_ptr_multiple_inheritance) {
    // without the _ptr shadowed in the shared_ptr, type conversions in cases of multiple inheritances silently corrupt
    // memory. this test is to make sure we handle this behaviour correctly

    struct IReadable {
        int read_state{1};
        virtual ~IReadable() = default;
    };
    struct IWritable {
        int write_state{2};
    };
    struct Device : IReadable, IWritable {
        int device_state{3};
    };

    spn::Heap<512> heap;
    auto           device = heap.make_shared<Device>(heap, K_NO_WAIT);

    spn::shared_ptr<IWritable> writable = device;

    zassert_not_equal(
        reinterpret_cast<uintptr_t>(device.get()),
        reinterpret_cast<uintptr_t>(writable.get()),
        "must adjust pointer for multiple inheritance"
    );

    zassert_equal(writable->write_state, 2, "must access correct base member");
    zassert_equal(writable.use_count(), 2, "must share ownership");
}

ZTEST(shared_ptr_suite, test_shared_ptr_overaligned_allocation) {
    struct alignas(16) OverAligned {
        int data[4]{};
    };

    spn::Heap<512> heap;
    auto           ptr = heap.make_shared<OverAligned>(heap, K_NO_WAIT);

    zassert_not_null(ptr.get(), "must succeed");
    zassert_equal(reinterpret_cast<uintptr_t>(ptr.get()) % 16, 0U, "must respect alignment");
}

ZTEST(shared_ptr_suite, test_shared_ptr_assign_shared_control) {
    struct Base {
        int x{42};
        virtual ~Base() = default;
    };
    struct Derived : Base {
        int y{84};
    };

    spn::Heap<512> heap;
    auto           derived = heap.make_shared<Derived>(heap, K_NO_WAIT);
    auto           copy    = derived;
    auto           base    = spn::shared_ptr<Base>(derived);
    zassert_equal(derived.use_count(), 3, "must share control block");

    // same-type copy with shared control: no-op optimization
    derived = copy;
    zassert_equal(derived.use_count(), 3, "must not change refcount on same-control copy");

    // cross-type copy with shared control: no-op optimization
    base = derived;
    zassert_equal(base.use_count(), 3, "must not change refcount on cross-type same-control copy");

    // same-type move with shared control: decrements refcount and empties source
    derived = etl::move(copy);
    zassert_equal(derived.use_count(), 2, "must decrement refcount on same-control move");
    zassert_false(copy, "must empty source on same-control move");

    // cross-type move with shared control: decrements refcount and empties source
    auto temp = base;
    base      = etl::move(temp);
    zassert_equal(base.use_count(), 2, "must decrement refcount on cross-type same-control move");
    zassert_false(temp, "must empty source on cross-type same-control move");

    // copy assignment with different control blocks
    auto other = heap.make_shared<Derived>(heap, K_NO_WAIT);
    other->x   = 100;
    derived    = other;
    zassert_equal(derived->x, 100, "must adopt new object");
    zassert_equal(derived.use_count(), 2, "must share new control block");

    // self-assignment
    derived = derived;
    zassert_equal(derived.use_count(), 2, "must preserve state on self-assignment");
}

ZTEST(shared_ptr_suite, test_shared_ptr_allocation_failure) {
    struct VeryLarge {
        char data[512];
    };

    spn::Heap<256> heap;
    auto           fail_obj = heap.make_shared<VeryLarge>(heap, K_NO_WAIT);
    zassert_false(fail_obj, "must fail when object oversized");

    CountingPayload::destroyed = 0;
    spn::Heap<80> tiny_ctrl; // 80 byte minimum on 64 bit platforms
    auto          fail_ctrl = heap.make_shared<CountingPayload>(tiny_ctrl, K_NO_WAIT);
    zassert_false(fail_ctrl, "must fail when control heap too small");
    zassert_equal(CountingPayload::destroyed, 1, "must clean up object on control failure");
}

ZTEST(shared_ptr_suite, test_shared_ptr_self_move) {
    spn::Heap<256> heap;
    auto           ptr = heap.make_shared<TestPayload>(heap, K_NO_WAIT, 42, 84);
    TestPayload*   raw = ptr.get();

    ptr = etl::move(ptr);
    zassert_equal(ptr.get(), raw, "must preserve pointer on self-move");
    zassert_equal(ptr->a, 42, "must keep object accessible on self-move");
}