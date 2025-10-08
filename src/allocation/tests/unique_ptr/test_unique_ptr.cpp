#include "spn/allocation/heap.hpp"
#include "spn/allocation/pool.hpp"
#include "spn/allocation/slab.hpp"
#include "spn/allocation/unique_ptr.hpp"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

// Note: ETL extensively tests unique_ptr core functionality.
// These minimal tests only verify our IHeap integration.

namespace {
class TestObj {
public:
    static int count;
    TestObj() { ++count; }
    ~TestObj() { --count; }
};
int TestObj::count = 0;

struct alignas(16) AlignedTestObj {
    static int count;
    AlignedTestObj() { ++count; }
    ~AlignedTestObj() { --count; }
    char _pad[16];
};
int AlignedTestObj::count = 0;
} // namespace

ZTEST_SUITE(unique_ptr_suite, nullptr, nullptr, nullptr, nullptr, nullptr);

ZTEST(unique_ptr_suite, test_heap_integration) {
    auto heap      = spn::Heap<256>{};
    TestObj::count = 0;

    // Test make_unique success + cleanup
    {
        auto ptr = heap.make_unique<TestObj>(K_NO_WAIT);
        zassert_true(ptr && TestObj::count == 1, "heap allocation + construction");
    }
    zassert_equal(TestObj::count, 0, "heap cleanup");

    // Test make_unique failure
    void* fill = nullptr;
    zassert_equal(heap.alloc(&fill, 200, K_NO_WAIT), 0, "fill heap");
    auto ptr = heap.make_unique<TestObj>(K_NO_WAIT);
    zassert_false(ptr, "allocation failure");
    heap.release(fill);
}

ZTEST(unique_ptr_suite, test_pool_slab_integration) {
    TestObj::count = 0;

    // Test Pool integration
    spn::Pool<TestObj, 2> pool;
    zassert_equal(TestObj::count, 2, "two objects should be default constructed");
    {
        auto ptr = pool.make_unique(K_NO_WAIT);
        zassert_true(ptr, "pool should allocate");
        zassert_equal(TestObj::count, 2, "TestObj should not have been constructed");
        zassert_equal(pool.used(), 1, "pool should report allocation");
    }
    zassert_equal(TestObj::count, 2, "pool cleanup");
    zassert_equal(pool.used(), 0, "pool should be empty after cleanup");

    // Test Slab integration with properly aligned type
    AlignedTestObj::count = 0;
    spn::Slab<AlignedTestObj, 2> slab;
    {
        auto ptr = slab.make_unique(K_NO_WAIT);
        zassert_true(ptr && AlignedTestObj::count == 1, "slab allocation + construction");
        zassert_equal(slab.allocated(), 1U, "slab should report allocation");
    }
    zassert_equal(AlignedTestObj::count, 0, "slab cleanup");
    zassert_equal(slab.allocated(), 0U, "slab should be empty after cleanup");
}
