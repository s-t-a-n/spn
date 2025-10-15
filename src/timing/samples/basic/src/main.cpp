#include "spn/timing/elapsed_timer.hpp"
#include "spn/timing/monotonic_clock.hpp"
#include "spn/timing/monotonic_timer.hpp"
#include "spn/timing/periodic_timers.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(timing_sample, LOG_LEVEL_INF);

using namespace spn::timing;
using namespace etl::chrono_literals;

void demonstrate_elapsed_timer() {
    LOG_INF("ElapsedTimer demo");

    ElapsedTimer timer;

    timer.start();
    k_msleep(50);
    timer.stop();

    LOG_INF("Elapsed: %llu ms", timer.elapsed_ms());
    LOG_INF("Elapsed: %llu us", timer.elapsed_us());
    LOG_INF("Elapsed: %llu ns", timer.elapsed_ns());
}

void demonstrate_monotonic_clock() {
    LOG_INF("MonotonicClock demo");

    auto ms_duration = 250_ms;
    auto us_duration = 500000_us;
    auto ns_duration = 1000000_ns;

    LOG_INF("Duration conversions:");
    LOG_INF("  250ms = %lld us = %lld ns", to_us(ms_duration), to_ns(ms_duration));
    LOG_INF("  500000us = %lld ms", to_ms(us_duration));
    LOG_INF("  1000000ns = %lld us", to_us(ns_duration));

    LOG_INF("UptimeClock measurements:");
    auto start_time = UptimeClock::now();
    k_msleep(50);
    auto end_time         = UptimeClock::now();
    auto elapsed_duration = end_time - start_time;

    LOG_INF("  Elapsed: %lld ms (uptime clock)", elapsed_duration.count());
    LOG_INF("  System uptime: %llu ms", UptimeClock::uptime_ms());

    LOG_INF("CycleClock measurements:");
    auto cycle_start = CycleClock::now();
    {
        k_busy_wait(10); // do actual work
    }
    auto cycle_end     = CycleClock::now();
    auto cycle_elapsed = cycle_end - cycle_start;

    LOG_INF("  Computation cycles: %llu", cycle_elapsed.count());
    LOG_INF("  Estimated time: %llu us", CycleClock::cycles_to_us(cycle_elapsed.count()));

    LOG_INF("MonotonicTimer demonstrations:");

    UptimeTimer uptime_timer;
    uptime_timer.start();
    k_msleep(25);
    auto uptime_elapsed = uptime_timer.stop();
    LOG_INF("  UptimeTimer: %lld ms", to_ms(uptime_elapsed));

    CycleTimer cycle_timer;
    cycle_timer.start();
    {
        k_busy_wait(20); // do actual work
    }
    auto timer_cycles = cycle_timer.stop();
    LOG_INF("  CycleTimer: %lld cycles (%lld us)", timer_cycles.count(), to_us(timer_cycles));

    auto duration1     = 100_ms;
    auto duration2     = 50_ms;
    auto sum_duration  = duration1 + duration2;
    auto diff_duration = duration1 - duration2;

    LOG_INF("Duration arithmetic:");
    LOG_INF("  100ms + 50ms = %lld ms", sum_duration.count());
    LOG_INF("  100ms - 50ms = %lld ms", diff_duration.count());
    LOG_INF("  100ms * 2 = %llu ms", (duration1 * 2).count());

    LOG_INF("Sleep demonstration:");
    auto sleep_start = UptimeClock::now();
    sleep_for(20_ms);
    auto sleep_end    = UptimeClock::now();
    auto actual_sleep = sleep_end - sleep_start;
    LOG_INF("  Requested 20ms, actual: %llu ms", actual_sleep.count());
}

static volatile bool oneshot_callback_fired = false;

static void oneshot_callback() {
    LOG_INF("OneShotTimer fired!");
    oneshot_callback_fired = true;
}

void demonstrate_oneshot_timer() {
    LOG_INF("OneShotTimer demo");

    OneShotTimer timer;
    oneshot_callback_fired = false;

    timer.start(200_ms, timer_callback_f::create<oneshot_callback>());

    LOG_INF("OneShotTimer started, remaining: %lld ms", to_ms(timer.remaining()));

    while (!oneshot_callback_fired) {
        k_msleep(50);
        if (timer.is_running()) {
            LOG_INF("Timer still running, remaining: %lld ms", to_ms(timer.remaining()));
        }
    }

    LOG_INF("OneShotTimer demonstration completed");
}

static volatile int periodic_callback_count = 0;

static void periodic_callback() {
    periodic_callback_count++;
    LOG_INF("Periodic timer fired #%d", periodic_callback_count);
}

void demonstrate_periodic_timer() {
    LOG_INF("PeriodicTimer demo");

    PeriodicTimer timer;
    periodic_callback_count = 0;

    timer.start(100_ms, timer_callback_f::create<periodic_callback>());

    LOG_INF("PeriodicTimer started with period: %lld ms", to_ms(timer.period()));

    k_msleep(450);

    timer.stop();
    LOG_INF("PeriodicTimer stopped after %d callbacks", periodic_callback_count);
}

int main(void) {
    demonstrate_elapsed_timer();
    k_msleep(200);

    demonstrate_monotonic_clock();
    k_msleep(200);

    demonstrate_oneshot_timer();
    k_msleep(200);

    demonstrate_periodic_timer();
    return 0;
}