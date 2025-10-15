#include "spn/timing/periodic_timers.hpp"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

using namespace spn::timing;
using namespace etl::chrono_literals;

namespace {

struct CallbackContext {
    volatile int count = 0;

    void increment() { count++; }

    timer_callback_f make_callback() {
        return timer_callback_f::create<CallbackContext, &CallbackContext::increment>(*this);
    }
};

} // namespace

ZTEST_SUITE(periodic_timers_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(periodic_timers_suite, test_oneshot_timer_basic) {
    CallbackContext ctx;
    OneShotTimer    timer;

    timer.start(50_ms, ctx.make_callback());

    zassert_true(timer.is_running(), "must be running after start");
    zassert_true(to_ms(timer.remaining()) > 0, "must have remaining time");

    k_msleep(100);

    zassert_equal(1, ctx.count, "must invoke callback once");
    zassert_false(timer.is_running(), "must not be running after expiry");
    zassert_equal(0, to_ms(timer.remaining()), "must return zero remaining time after expiry");
}

ZTEST(periodic_timers_suite, test_periodic_timer_basic) {
    CallbackContext ctx;
    PeriodicTimer   timer;

    timer.start(30_ms, ctx.make_callback());

    zassert_true(timer.is_running(), "must be running after start");
    zassert_equal(30, to_ms(timer.period()), "must return configured period");
    zassert_true(to_ms(timer.remaining()) > 0, "must have remaining time");

    k_msleep(100);

    zassert_true(ctx.count >= 2, "must invoke callback repeatedly for periodic timer");
    zassert_true(ctx.count <= 4, "must invoke callback reasonable number of times");
    zassert_true(timer.is_running(), "must still be running");
}

ZTEST(periodic_timers_suite, test_periodic_timer_with_initial_delay) {
    CallbackContext ctx;
    PeriodicTimer   timer;

    timer.start(50_ms, 25_ms, ctx.make_callback());

    zassert_true(timer.is_running(), "must be running after start");
    zassert_equal(25, to_ms(timer.period()), "must return period from second parameter");

    k_msleep(30);
    zassert_equal(0, ctx.count, "must not invoke callback before initial delay");

    k_msleep(30);
    zassert_true(ctx.count >= 1, "must invoke callback after initial delay");

    timer.stop();
}

ZTEST(periodic_timers_suite, test_destructor_stops_timer) {
    CallbackContext oneshot_ctx;
    CallbackContext periodic_ctx;

    {
        OneShotTimer timer;
        timer.start(50_ms, oneshot_ctx.make_callback());
        zassert_true(timer.is_running(), "must be running after start");
    }

    {
        PeriodicTimer timer;
        timer.start(30_ms, periodic_ctx.make_callback());
        zassert_true(timer.is_running(), "must be running after start");
    }

    const int periodic_count = periodic_ctx.count;
    k_msleep(100);

    zassert_equal(0, oneshot_ctx.count, "must stop oneshot timer in destructor");
    zassert_equal(periodic_count, periodic_ctx.count, "must stop periodic timer in destructor");
}

ZTEST(periodic_timers_suite, test_restart_stops_previous_timer) {
    CallbackContext ctx;
    OneShotTimer    timer;

    timer.start(100_ms, ctx.make_callback());
    k_msleep(20);
    timer.start(50_ms, ctx.make_callback());
    k_msleep(80);

    zassert_equal(1, ctx.count, "must invoke callback once with new duration");
    zassert_false(timer.is_running(), "must not be running after expiry");
}

ZTEST(periodic_timers_suite, test_stop_is_idempotent) {
    CallbackContext ctx;
    OneShotTimer    timer;

    timer.stop();
    zassert_false(timer.is_running(), "must handle stop on uninitialized timer");

    timer.start(100_ms, ctx.make_callback());
    timer.stop();
    timer.stop();
    k_msleep(150);

    zassert_false(timer.is_running(), "must handle multiple stop calls");
    zassert_equal(0, ctx.count, "must not invoke callback after stop");
}
