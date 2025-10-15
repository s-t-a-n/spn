#include "spn/timing/elapsed_timer.hpp"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

using namespace spn::timing;

ZTEST_SUITE(elapsed_timer_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(elapsed_timer_suite, test_elapsed_timer_basic) {
    ElapsedTimer timer;

    timer.start();
    k_msleep(10);
    timer.stop();

    auto elapsed_ms = timer.elapsed_ms();
    auto elapsed_us = timer.elapsed_us();
    auto elapsed_ns = timer.elapsed_ns();

    zassert_true(elapsed_ns > 0, "expected some elapsed time");
    zassert_true(elapsed_us > 0, "expected some elapsed time");
    zassert_true(elapsed_ms < 1000, "elapsed time too large");
}

ZTEST(elapsed_timer_suite, test_elapsed_timer_zero_time) {
    ElapsedTimer timer;

    timer.start();
    timer.stop();

    auto elapsed_ms = timer.elapsed_ms();
    zassert_true(elapsed_ms < 10);
}

ZTEST(elapsed_timer_suite, test_elapsed_timer_ordering) {
    ElapsedTimer timer1, timer2;

    timer1.start();
    k_msleep(5);
    timer2.start();
    k_msleep(5);
    timer1.stop();
    timer2.stop();

    zassert_true(timer1.elapsed_ms() >= timer2.elapsed_ms());
}