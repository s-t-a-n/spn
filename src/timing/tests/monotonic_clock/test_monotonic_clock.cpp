#include "spn/timing/monotonic_clock.hpp"
#include "spn/timing/monotonic_timer.hpp"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

using namespace spn::timing;
using namespace etl::chrono_literals;

ZTEST_SUITE(monotonic_clock_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(monotonic_clock_suite, test_conversion_helpers) {
    zassert_equal(100, to_ms(100_ms), "must convert ms to ms");
    zassert_equal(100000, to_us(100_ms), "must convert ms to us");
    zassert_equal(100000000, to_ns(100_ms), "must convert ms to ns");

    zassert_equal(1, to_ms(1000_us), "must convert us to ms");
    zassert_equal(500, to_us(500000_ns), "must convert ns to us");
    zassert_equal(0, to_ms(500_us), "must truncate sub ms to zero");

    zassert_equal(2000, to_ms(chrono::seconds(2)), "must convert seconds to ms");

    Ticks ticks(CONFIG_SYS_CLOCK_TICKS_PER_SEC);
    zassert_equal(CONFIG_SYS_CLOCK_TICKS_PER_SEC, ticks.count(), "must construct ticks duration");
    auto ticks_as_ms = chrono::duration_cast<chrono::milliseconds>(ticks);
    zassert_equal(1000, ticks_as_ms.count(), "must convert ticks to ms");

    Cycles cycles(10000);
    zassert_equal(10000, cycles.count(), "must construct cycles duration");
}

ZTEST(monotonic_clock_suite, test_time_point_operators) {
    UptimeClock::time_point tp1(100_ms);
    UptimeClock::time_point tp2(200_ms);

    auto duration_between = tp2 - tp1;
    zassert_equal(100, duration_between.count(), "must subtract time_point - time_point");

    auto tp3 = tp1 + 50_ms;
    zassert_equal(150, tp3.time_since_epoch().count(), "must add time_point + duration");

    auto tp4 = tp2 - 50_ms;
    zassert_equal(150, tp4.time_since_epoch().count(), "must subtract time_point - duration");

    auto tp5 = 50_ms + tp1;
    zassert_equal(150, tp5.time_since_epoch().count(), "must add duration + time_point");
}

ZTEST(monotonic_clock_suite, test_uptime_clock) {
    auto time1 = UptimeClock::now();
    k_msleep(10);
    auto time2 = UptimeClock::now();

    zassert_true(time2 > time1, "must be monotonic");
    auto elapsed = time2 - time1;
    zassert_true(elapsed.count() >= 8, "must track elapsed time");

    uint64_t uptime_ms = UptimeClock::uptime_ms();
    zassert_true(uptime_ms > 0, "must provide uptime_ms");

    k_ticks_t uptime_ticks = UptimeClock::uptime_ticks();
    zassert_true(uptime_ticks > 0, "must provide uptime_ticks");

    zassert_true(UptimeClock::is_steady, "must be steady clock");
}

ZTEST(monotonic_clock_suite, test_cycle_clock) {
    uint64_t cycles1 = CycleClock::cycles();
    k_busy_wait(10);
    uint64_t cycles2 = CycleClock::cycles();
    zassert_true(cycles2 > cycles1, "must be monotonic");

    k_busy_wait(10);
    uint32_t cycles_32 = CycleClock::cycles_32();
    zassert_true(cycles_32 > 0, "must provide cycles_32");

    uint64_t ns = CycleClock::cycles_to_ns(1000000);
    uint64_t us = CycleClock::cycles_to_us(1000000);
    zassert_true(ns >= us, "must convert cycles to nanoseconds and microseconds");

    auto time1 = CycleClock::now();
    k_busy_wait(10);
    auto time2 = CycleClock::now();
    zassert_true(time2 > time1, "must provide time_point via .now()");

    zassert_true(CycleClock::is_steady, "must be steady clock");
}

ZTEST(monotonic_clock_suite, test_monotonic_timer) {
    UptimeTimer timer;
    zassert_false(timer.is_running(), "must not be running initially");

    timer.start();
    zassert_true(timer.is_running(), "must be running after start");

    k_msleep(10);
    auto elapsed_while_running = to_ms(timer.elapsed());
    zassert_true(elapsed_while_running >= 8, "must track elapsed time while running");

    auto final_duration = timer.stop();
    zassert_false(timer.is_running(), "must not be running after stop");
    zassert_true(final_duration.count() >= 8, "must return final duration on stop");

    k_msleep(10);
    auto elapsed_after_stop = to_ms(timer.elapsed());
    zassert_equal(final_duration.count(), elapsed_after_stop, "must freeze elapsed time after stop");

    timer.reset();
    zassert_false(timer.is_running(), "must not be running after reset");
    zassert_equal(0, timer.elapsed().count(), "must clear elapsed time on reset");
}

ZTEST(monotonic_clock_suite, test_sleep_for) {
    auto start = UptimeClock::now();
    sleep_for(10_ms);
    auto elapsed = UptimeClock::now() - start;
    zassert_true(elapsed.count() >= 8, "must sleep for milliseconds via k_sleep");

    uint64_t start_cycles = CycleClock::cycles();
    sleep_for(5_us);
    uint64_t elapsed_cycles = CycleClock::cycles() - start_cycles;
    zassert_true(elapsed_cycles >= k_ns_to_cyc_ceil64(4000), "must sleep for microseconds via k_busy_wait");

    start_cycles = CycleClock::cycles();
    sleep_for(500_ns);
    elapsed_cycles = CycleClock::cycles() - start_cycles;
    zassert_true(elapsed_cycles >= k_ns_to_cyc_ceil64(400), "must sleep for nanoseconds via cycle busy-wait");

    start = UptimeClock::now();
    sleep_for(chrono::milliseconds(0));
    elapsed = UptimeClock::now() - start;
    zassert_true(elapsed.count() < 5, "must return immediately for zero duration");

    start = UptimeClock::now();
    sleep_for(chrono::milliseconds(-100));
    elapsed = UptimeClock::now() - start;
    zassert_true(elapsed.count() < 5, "must return immediately for negative duration");

    start_cycles = CycleClock::cycles();
    sleep_for(1500_us);
    elapsed_cycles = CycleClock::cycles() - start_cycles;
    zassert_true(elapsed_cycles >= k_ns_to_cyc_ceil64(1200000), "must sleep 1ms + 500us remainder");
}

ZTEST(monotonic_clock_suite, test_sleep_until) {
    auto start  = UptimeClock::now();
    auto target = start + 10_ms;
    sleep_until(target);
    auto end = UptimeClock::now();
    zassert_true(end >= target, "must sleep until target time");

    start     = UptimeClock::now();
    auto past = start - 100_ms;
    sleep_until(past);
    end = UptimeClock::now();
    zassert_true((end - start).count() < 5, "must return immediately for past time point");
}

ZTEST(monotonic_clock_suite, test_to_timeout) {
    auto timeout1 = to_timeout(100_ms);
    zassert_true(K_TIMEOUT_EQ(timeout1, K_MSEC(100)), "must convert 100ms to timeout");

    auto timeout2 = to_timeout(0_ms);
    zassert_true(K_TIMEOUT_EQ(timeout2, K_MSEC(0)), "must convert zero duration to zero timeout");

    auto timeout3 = to_timeout(1_us);
    zassert_true(K_TIMEOUT_EQ(timeout3, K_MSEC(0)), "must truncate sub-millisecond to zero");

    auto timeout4 = to_timeout(chrono::seconds(60));
    zassert_true(K_TIMEOUT_EQ(timeout4, K_MSEC(60000)), "must convert 60 seconds to 60000ms");

    auto large_duration = chrono::milliseconds(static_cast<int64_t>(UINT32_MAX) - 1);
    auto timeout5       = to_timeout(large_duration);
    zassert_true(K_TIMEOUT_EQ(timeout5, K_MSEC(UINT32_MAX - 1)), "must handle near maximum duration");

    auto max_duration = chrono::milliseconds(static_cast<int64_t>(UINT32_MAX));
    auto timeout6     = to_timeout(max_duration);
    zassert_true(K_TIMEOUT_EQ(timeout6, K_MSEC(UINT32_MAX)), "must handle UINT32_MAX duration");
}