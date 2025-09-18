#include <spn/allocation/slab.hpp>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_slab, LOG_LEVEL_INF);

ZTEST_SUITE(slab_suite, NULL, NULL, NULL, NULL, NULL);

/// Test basic slab block allocation
ZTEST(slab_suite, test_slab_basic_allocation) {
    constexpr auto num_blocks = 5;
    using value_t             = uint8_t[32];
    auto slab                 = spn::Slab<value_t, num_blocks>{};

    value_t* ptr = nullptr;
    zassert_equal(slab.alloc(&ptr), 0, "Basic slab allocation should succeed");
    zassert_not_null(ptr, "Allocated block should not be null");

    // test that we can write to the allocated block
    memset(ptr, 0xAA, sizeof(value_t));

    zassert_equal(slab.release(ptr), 0, "Block release should succeed");
}

/// Test allocating all slab blocks
ZTEST(slab_suite, test_slab_full_allocation) {
    constexpr auto num_blocks = 10;
    using value_t             = uint8_t[32];
    auto slab                 = spn::Slab<value_t, num_blocks>{};

    value_t* ref_store[num_blocks]{};

    // allocate all blocks
    for (int i = 0; i < num_blocks; i++) {
        zassert_equal(slab.alloc(&ref_store[i]), 0, "Block %d allocation should succeed", i);
        zassert_not_null(ref_store[i], "Block %d should not be null", i);

        // write unique pattern to each block
        memset(ref_store[i], i, sizeof(value_t));
    }

    // try to allocate when all blocks are used - should fail
    value_t* overflow_ptr = nullptr;
    zassert_not_equal(slab.alloc(&overflow_ptr), 0, "Allocation when full should fail");
    zassert_is_null(overflow_ptr, "Failed allocation should return null");

    // release all blocks
    for (int i = 0; i < num_blocks; i++) {
        zassert_equal(slab.release(ref_store[i]), 0, "Block %d release should succeed", i);
    }
}

/// Test allocation failure when slab is full
ZTEST(slab_suite, test_slab_allocation_failure) {
    constexpr auto num_blocks = 3;
    using value_t             = uint8_t[16];
    auto slab                 = spn::Slab<value_t, num_blocks>{};

    value_t* ptrs[num_blocks];

    // fill all blocks
    for (int i = 0; i < num_blocks; i++) {
        zassert_equal(slab.alloc(&ptrs[i]), 0, "Allocation %d should succeed", i);
    }

    // next allocation should fail
    value_t* extra_ptr = nullptr;
    zassert_not_equal(slab.alloc(&extra_ptr), 0, "Extra allocation should fail");
    zassert_is_null(extra_ptr, "Failed allocation should return null");

    // release all blocks
    for (int i = 0; i < num_blocks; i++) {
        zassert_equal(slab.release(ptrs[i]), 0, "Release %d should succeed", i);
    }
}

/// Test block release and reallocation
ZTEST(slab_suite, test_slab_release_and_realloc) {
    constexpr auto num_blocks = 5;
    using value_t             = uint8_t[64];
    auto slab                 = spn::Slab<value_t, num_blocks>{};

    // allocate a block
    value_t* ptr1 = nullptr;
    zassert_equal(slab.alloc(&ptr1), 0, "First allocation should succeed");
    auto first_address = ptr1;

    // release and reallocate
    zassert_equal(slab.release(ptr1), 0, "Release should succeed");

    value_t* ptr2 = nullptr;
    zassert_equal(slab.alloc(&ptr2), 0, "Reallocation should succeed");
    zassert_equal(ptr2, first_address, "Should reuse the same block");

    zassert_equal(slab.release(ptr2), 0, "Second release should succeed");
}

/// Test multiple allocation and release cycles
ZTEST(slab_suite, test_slab_multiple_alloc_release_cycles) {
    constexpr auto num_blocks = 4;
    using value_t             = uint8_t[128];
    auto slab                 = spn::Slab<value_t, num_blocks>{};

    // perform multiple allocation/release cycles
    for (int cycle = 0; cycle < 3; cycle++) {
        value_t* ptrs[num_blocks];

        // allocate all blocks in this cycle
        for (int i = 0; i < num_blocks; i++) {
            zassert_equal(slab.alloc(&ptrs[i]), 0, "Cycle %d allocation %d should succeed", cycle, i);

            // write cycle-specific pattern
            memset(ptrs[i], cycle * 10 + i, sizeof(value_t));
        }

        // verify allocation failure when full
        value_t* extra = nullptr;
        zassert_not_equal(slab.alloc(&extra), 0, "Cycle %d extra allocation should fail", cycle);

        // release all blocks
        for (int i = 0; i < num_blocks; i++) {
            zassert_equal(slab.release(ptrs[i]), 0, "Cycle %d release %d should succeed", cycle, i);
        }
    }

    // final test - should be able to allocate again
    value_t* final_ptr = nullptr;
    zassert_equal(slab.alloc(&final_ptr), 0, "Final allocation should succeed");
    zassert_equal(slab.release(final_ptr), 0, "Final release should succeed");
}

/// Test interleaved allocation and release operations
ZTEST(slab_suite, test_slab_interleaved_operations) {
    constexpr auto num_blocks = 6;
    using value_t             = uint8_t[48];
    auto slab                 = spn::Slab<value_t, num_blocks>{};

    value_t* ptr1 = nullptr;
    value_t* ptr2 = nullptr;
    value_t* ptr3 = nullptr;

    // allocate some blocks
    zassert_equal(slab.alloc(&ptr1), 0, "First allocation should succeed");
    zassert_equal(slab.alloc(&ptr2), 0, "Second allocation should succeed");
    zassert_equal(slab.alloc(&ptr3), 0, "Third allocation should succeed");

    // release middle block
    zassert_equal(slab.release(ptr2), 0, "Middle block release should succeed");

    // allocate new block - should reuse released slot
    value_t* ptr4 = nullptr;
    zassert_equal(slab.alloc(&ptr4), 0, "Fourth allocation should succeed");
    zassert_equal(ptr4, ptr2, "Should reuse released block");

    // clean up
    zassert_equal(slab.release(ptr1), 0, "Cleanup 1 should succeed");
    zassert_equal(slab.release(ptr3), 0, "Cleanup 3 should succeed");
    zassert_equal(slab.release(ptr4), 0, "Cleanup 4 should succeed");
}