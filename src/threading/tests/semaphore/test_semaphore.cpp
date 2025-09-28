#include "spn/threading/semaphore.hpp"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

namespace {

ZTEST_SUITE(semaphore_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(semaphore_suite, semaphore_basic_operations) {
    spn::Semaphore<1, 5> sem;

    zassert_equal(sem.count(), 1, "initial count should match template parameter");
    zassert_equal(sem.limit(), 5, "limit should match template parameter");

    int result = sem.take(K_NO_WAIT);
    zassert_ok(result, "take should return 0 on success");
    zassert_equal(sem.count(), 0, "count should be decremented after take");

    sem.give();
    zassert_equal(sem.count(), 1, "count should be incremented after give");
}

ZTEST(semaphore_suite, semaphore_take_timeout) {
    spn::Semaphore<0, 5> sem;

    int result = sem.take(K_NO_WAIT);
    zassert_not_equal(result, 0, "take should return non-zero on timeout");
}

} // namespace