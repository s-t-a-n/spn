#include "spn/threading/work.hpp"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

namespace {

constexpr k_timeout_t TestTimeout = K_MSEC(250);

// to test Work instances with void args
struct VoidWorkContext {
    k_sem                            handled;
    int                              call_count            = 0;
    bool                             cancel_inside_handler = false;
    spn::Work<void, VoidWorkContext> work;

    VoidWorkContext() : work(this, &VoidWorkContext::entry) { k_sem_init(&handled, 0, 2); }

    void entry() {
        ++call_count;
        if (cancel_inside_handler) {
            work.cancel();
        }
        k_sem_give(&handled);
    }
};

// to test Work instances with args
struct ArgWorkContext {
    k_sem                          handled;
    int                            call_count = 0;
    int*                           last_arg   = nullptr;
    spn::Work<int, ArgWorkContext> work;

    ArgWorkContext() : work(this, &ArgWorkContext::entry) { k_sem_init(&handled, 0, 2); }

    void entry(int* value) {
        last_arg = value;
        ++call_count;
        k_sem_give(&handled);
    }
};

} // namespace

ZTEST_SUITE(work_suite, nullptr, nullptr, nullptr, nullptr, nullptr);

ZTEST(work_suite, work_schedule_runs_handler_once) {
    VoidWorkContext owner;

    int schedule_rc = owner.work.schedule(K_MSEC(10));
    zassert_true(schedule_rc >= 0, "schedule should succeed");
    zassert_true(owner.work.is_scheduled(), "work should be pending before execution");

    zassert_ok(k_sem_take(&owner.handled, TestTimeout), "handler should run");
    zassert_equal(owner.call_count, 1, "handler should run exactly once");

    k_sleep(K_MSEC(1));
    zassert_false(owner.work.is_scheduled(), "work should clear pending state after run");
}

ZTEST(work_suite, work_cancel_prevents_execution) {
    VoidWorkContext owner;

    zassert_true(owner.work.schedule(K_MSEC(100)) >= 0, "schedule should succeed");
    zassert_true(owner.work.is_scheduled(), "work should report pending before cancel");

    owner.work.cancel();
    zassert_false(owner.work.is_scheduled(), "work should no longer be pending after cancel");

    int rc = k_sem_take(&owner.handled, K_MSEC(100));
    zassert_equal(rc, -EAGAIN, "handler should not run after cancel");
    zassert_equal(owner.call_count, 0, "cancelled work should not invoke handler");
}

ZTEST(work_suite, work_cancel_inside_handler_is_safe) {
    VoidWorkContext owner;
    owner.cancel_inside_handler = true;

    zassert_true(owner.work.schedule(K_NO_WAIT) >= 0, "schedule should succeed");
    zassert_ok(k_sem_take(&owner.handled, TestTimeout), "handler should still run once");
    zassert_equal(owner.call_count, 1, "handler should run exactly once");

    owner.cancel_inside_handler = false;
    zassert_true(owner.work.schedule(K_NO_WAIT) >= 0, "schedule should succeed after in-handler cancel");
    zassert_ok(k_sem_take(&owner.handled, TestTimeout), "work should remain usable after in-handler cancel");
    zassert_equal(owner.call_count, 2, "handler should run again after reuse");
}

ZTEST(work_suite, work_with_argument_passes_pointer) {
    ArgWorkContext owner;
    int            value = 42;

    zassert_true(owner.work.schedule(K_NO_WAIT, &value) >= 0, "schedule should succeed with argument");

    zassert_ok(k_sem_take(&owner.handled, TestTimeout), "handler should run with argument");
    zassert_equal(owner.last_arg, &value, "handler should receive scheduled argument");
    zassert_equal(owner.call_count, 1, "argument handler should run exactly once");
}

ZTEST(work_suite, work_reschedule_uses_latest_argument) {
    ArgWorkContext owner;
    int            first  = 1;
    int            second = 2;

    zassert_true(owner.work.schedule(K_MSEC(100), &first) >= 0, "initial schedule should succeed");
    k_sleep(K_MSEC(10));
    zassert_true(owner.work.schedule(K_NO_WAIT, &second) >= 0, "reschedule should succeed");

    zassert_ok(k_sem_take(&owner.handled, TestTimeout), "handler should run for rescheduled work");
    zassert_equal(owner.last_arg, &second, "handler should see most recent argument");
    zassert_equal(owner.call_count, 1, "reschedule should result in a single handler call");
}

ZTEST(work_suite, work_flush_waits_and_reports) {
    VoidWorkContext owner;

    zassert_true(owner.work.schedule(K_MSEC(50)) >= 0, "schedule should succeed");

    int flush_rc = owner.work.flush();
    zassert_equal(flush_rc, 1, "flush should wait for pending work");
    zassert_equal(owner.call_count, 1, "handler should run exactly once before flush returns");
    zassert_false(owner.work.is_scheduled(), "flush should leave work idle");

    int second_rc = owner.work.flush();
    zassert_equal(second_rc, 0, "flush should report no wait when nothing pending");
}
