#include "spn/timing/rate_control.hpp"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

using namespace spn::timing;
using namespace etl::chrono_literals;

ZTEST_SUITE(rate_control_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(rate_control_suite, test_rate_limiter) {
    RateLimiter limiter(10, 5);
    zassert_equal(10, limiter.rate_per_second(), "must store rate");
    zassert_equal(5, limiter.bucket_capacity(), "must store capacity");

    zassert_true(limiter.try_acquire(3), "must allow acquire within capacity");
    zassert_equal(2, limiter.available_tokens(), "must consume tokens");

    zassert_false(limiter.try_acquire(3), "must reject when insufficient tokens");
    zassert_equal(2, limiter.available_tokens(), "must not consume on reject");

    zassert_true(limiter.try_acquire(0), "must allow zero acquire");
    zassert_equal(2, limiter.available_tokens(), "must not consume zero");

    RateLimiter default_limiter(20, 0);
    zassert_equal(20, default_limiter.bucket_capacity(), "must default capacity to rate");
}

ZTEST(rate_control_suite, test_rate_limiter_refill) {
    RateLimiter limiter(5, 2);

    limiter.try_acquire(2);
    zassert_false(limiter.try_acquire(), "must reject when empty");

    k_msleep(250);
    zassert_true(limiter.try_acquire(), "must refill over time");
}

ZTEST(rate_control_suite, test_rate_limiter_reset) {
    RateLimiter limiter(10, 3);

    limiter.try_acquire(3);
    limiter.reset();
    zassert_equal(3, limiter.available_tokens(), "must refill entire bucket");
}

ZTEST(rate_control_suite, test_throttle) {
    Throttle throttle(50_ms);
    zassert_equal(50, to_ms(throttle.min_interval()), "must store interval");

    zassert_true(throttle.allow(), "must allow first operation");
    zassert_false(throttle.allow(), "must throttle immediate second");

    auto remaining = to_ms(throttle.time_until_allowed());
    zassert_true(remaining > 0 && remaining <= 50, "must return remaining time");

    k_msleep(60);
    zassert_equal(0, to_ms(throttle.time_until_allowed()), "must return 0 after interval");
    zassert_true(throttle.allow(), "must allow after interval");
}

ZTEST(rate_control_suite, test_throttle_reset) {
    Throttle throttle(100_ms);

    throttle.allow();
    throttle.reset();
    zassert_true(throttle.allow(), "must allow after reset");
}

ZTEST(rate_control_suite, test_deadline) {
    Deadline deadline(100_ms);
    zassert_equal(100, to_ms(deadline.deadline()), "must store deadline");
    zassert_false(deadline.is_active(), "must be inactive initially");

    deadline.start();
    zassert_true(deadline.is_active(), "must be active after start");
    zassert_false(deadline.is_exceeded(), "must not be exceeded immediately");

    k_msleep(50);
    zassert_false(deadline.is_exceeded(), "must not be exceeded within deadline");
    zassert_true(to_ms(deadline.elapsed()) > 0, "must return elapsed time when active");
    zassert_true(to_ms(deadline.remaining()) > 0, "must return remaining time when active");
    zassert_true(deadline.stop(), "must return true when met");
    zassert_false(deadline.is_active(), "must be inactive after stop");

    deadline.start();
    k_msleep(120);
    zassert_true(deadline.is_exceeded(), "must be exceeded after deadline");
    zassert_equal(0, to_ms(deadline.remaining()), "must return 0 remaining when exceeded");
    zassert_false(deadline.stop(), "must return false when exceeded");

    deadline.start();
    deadline.reset();
    zassert_false(deadline.is_active(), "must be inactive after reset");
    zassert_equal(0, to_ms(deadline.elapsed()), "must return 0 elapsed after reset");
}

ZTEST(rate_control_suite, test_deadline_inactive_state) {
    Deadline deadline(100_ms);

    zassert_true(deadline.stop(), "must return true when inactive");
    zassert_equal(0, to_ms(deadline.elapsed()), "must return 0 elapsed when inactive");
    zassert_equal(0, to_ms(deadline.remaining()), "must return 0 remaining when inactive");
    zassert_false(deadline.is_exceeded(), "must return false when inactive");

    deadline.start();
    k_msleep(20);
    deadline.start();
    k_msleep(30);
    auto elapsed = to_ms(deadline.elapsed());
    zassert_true(elapsed >= 25 && elapsed <= 40, "must reset on second start");
}

ZTEST(rate_control_suite, test_scoped_deadline) {
    ScopedDeadline scoped(50_ms);
    zassert_true(scoped.deadline().is_active(), "must auto-start on construction");

    k_msleep(30);
    zassert_false(scoped.is_exceeded(), "must not be exceeded within deadline");

    ScopedDeadline exceeded(30_ms);
    k_msleep(50);
    zassert_true(exceeded.is_exceeded(), "must be exceeded after deadline");
}