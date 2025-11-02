#include "spn/threading/thread.hpp"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

namespace {

constexpr k_timeout_t TestTimeout = K_MSEC(250);

struct ThreadHarness {
    k_sem         started;
    k_sem         running;
    k_sem         pausing;
    k_sem         resuming;
    k_sem         running_after_resume;
    k_sem         stopping;
    volatile bool awaiting_resume_run = false;
    int           running_calls       = 0;
    int           delegate_a_calls    = 0;
    int           delegate_b_calls    = 0;
};

void init_harness(ThreadHarness& harness) {
    k_sem_init(&harness.started, 0, 1);
    k_sem_init(&harness.running, 0, 1);
    k_sem_init(&harness.pausing, 0, 1);
    k_sem_init(&harness.resuming, 0, 1);
    k_sem_init(&harness.running_after_resume, 0, 1);
    k_sem_init(&harness.stopping, 0, 1);
    harness.awaiting_resume_run = false;
    harness.running_calls       = 0;
    harness.delegate_a_calls    = 0;
    harness.delegate_b_calls    = 0;
}

void thread_delegate(ThreadHarness* harness, spn::ThreadState state) {
    switch (state) {
    case spn::ThreadState::STARTING: k_sem_give(&harness->started); break;
    case spn::ThreadState::RUNNING:
        ++harness->running_calls;
        if (!harness->awaiting_resume_run && harness->running_calls == 1) {
            k_sem_give(&harness->running);
        }
        if (harness->awaiting_resume_run) {
            harness->awaiting_resume_run = false;
            k_sem_give(&harness->running_after_resume);
        }
        break;
    case spn::ThreadState::PAUSING: k_sem_give(&harness->pausing); break;
    case spn::ThreadState::RESUMING:
        harness->awaiting_resume_run = true;
        k_sem_give(&harness->resuming);
        break;
    case spn::ThreadState::STOPPING: k_sem_give(&harness->stopping); break;
    default: break;
    }
}

void thread_delegate_with_starting_delay(ThreadHarness* harness, spn::ThreadState state) {
    if (state == spn::ThreadState::STARTING) {
        k_sem_give(&harness->started);
        k_sleep(K_MSEC(100));
    } else {
        thread_delegate(harness, state);
    }
}

void delegate_a(ThreadHarness* harness, spn::ThreadState state) {
    if (state == spn::ThreadState::RUNNING) ++harness->delegate_a_calls;
    thread_delegate(harness, state);
}

void delegate_b(ThreadHarness* harness, spn::ThreadState state) {
    if (state == spn::ThreadState::RUNNING) ++harness->delegate_b_calls;
    thread_delegate(harness, state);
}

struct StepPacing : spn::threading::IPacingStrategy {
    StepPacing() {
        k_sem_init(&wait_sem, 0, 1);
        k_sem_init(&after_sem, 0, 1);
        k_sem_init(&wait_entered_sem, 0, 1);
        drain();
    }

    void on_enter_running() override {
        ++enter_count;
        drain();
    }

    void on_exit_running() override {
        ++exit_count;
        k_sem_give(&wait_sem);
    }

    void wait() override {
        k_sem_give(&wait_entered_sem);
        k_sem_take(&wait_sem, K_FOREVER);
    }

    void after_iteration() override { k_sem_give(&after_sem); }

    void interrupt() override { k_sem_give(&wait_sem); }

    void drain() {
        while (k_sem_take(&wait_sem, K_NO_WAIT) == 0) {
        }
        while (k_sem_take(&after_sem, K_NO_WAIT) == 0) {
        }
        while (k_sem_take(&wait_entered_sem, K_NO_WAIT) == 0) {
        }
    }

    int   enter_count = 0;
    int   exit_count  = 0;
    k_sem wait_sem;
    k_sem after_sem;
    k_sem wait_entered_sem;
};

using TestThread = spn::Thread<1024, ThreadHarness>;

} // namespace

ZTEST_SUITE(thread_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(thread_suite, test_thread_start_transitions_to_running) {
    ThreadHarness harness{};
    init_harness(harness);

    auto       delegate = TestThread::Delegate::create<thread_delegate>();
    TestThread thread(delegate, &harness, 5, "thread_start");

    zassert_equal(thread.state(), spn::ThreadState::IDLE, "thread should start idle");

    zassert_ok(thread.start());
    zassert_ok(k_sem_take(&harness.started, TestTimeout), "thread should report starting");
    zassert_ok(k_sem_take(&harness.running, TestTimeout), "thread should report running");
    zassert_equal(thread.state(), spn::ThreadState::RUNNING, "thread should be running");

    zassert_equal(thread.start(), -EINVAL, "start while running should fail");

    zassert_ok(thread.stop(TestTimeout));
    zassert_ok(k_sem_take(&harness.stopping, TestTimeout), "thread should report stopping");
    zassert_equal(thread.state(), spn::ThreadState::STOPPED, "thread should be stopped");
    zassert_ok(thread.stop(TestTimeout), "thread stop on stopped thread is successful no-op");
}

ZTEST(thread_suite, test_thread_pause_and_resume_transitions) {
    ThreadHarness harness{};
    init_harness(harness);

    auto       delegate = TestThread::Delegate::create<thread_delegate>();
    TestThread thread(delegate, &harness, 5, "thread_pause");

    zassert_ok(thread.start());
    zassert_ok(k_sem_take(&harness.started, TestTimeout), "thread should report starting");
    zassert_ok(k_sem_take(&harness.running, TestTimeout), "thread should report running");

    zassert_equal(thread.pause(TestTimeout), 0, "pause should succeed from running");
    zassert_ok(k_sem_take(&harness.pausing, TestTimeout), "delegate should observe pausing");
    zassert_equal(thread.state(), spn::ThreadState::PAUSED, "state should be paused");
    zassert_equal(thread.pause(K_NO_WAIT), 0, "pause while paused should be no-op");

    zassert_equal(thread.resume(), 0, "resume should transition to running");
    zassert_ok(k_sem_take(&harness.resuming, TestTimeout), "delegate should observe resuming");
    zassert_ok(k_sem_take(&harness.running_after_resume, TestTimeout), "delegate should run after resume");
    zassert_equal(thread.state(), spn::ThreadState::RUNNING, "state should return to running");

    zassert_ok(thread.stop(TestTimeout));
    zassert_ok(k_sem_take(&harness.stopping, TestTimeout), "thread should report stopping");
}

ZTEST(thread_suite, test_thread_start_or_resume_handles_states) {
    ThreadHarness harness{};
    init_harness(harness);

    auto       delegate = TestThread::Delegate::create<thread_delegate>();
    TestThread thread(delegate, &harness, 5, "thread_start_or_resume");

    zassert_ok(thread.start_or_resume());

    zassert_ok(k_sem_take(&harness.started, TestTimeout), "thread should report starting");
    zassert_ok(k_sem_take(&harness.running, TestTimeout), "thread should report running");
    zassert_equal(thread.start_or_resume(), -EINVAL, "start_or_resume while running should fail");

    zassert_equal(thread.pause(TestTimeout), 0, "pause should succeed");
    zassert_ok(k_sem_take(&harness.pausing, TestTimeout), "delegate should observe pausing");
    zassert_equal(thread.state(), spn::ThreadState::PAUSED, "state should reflect pause");

    zassert_ok(thread.start_or_resume());
    zassert_ok(k_sem_take(&harness.resuming, TestTimeout), "delegate should observe resuming");
    zassert_ok(k_sem_take(&harness.running_after_resume, TestTimeout), "delegate should run after resume");
    zassert_equal(thread.state(), spn::ThreadState::RUNNING, "state should return to running");

    zassert_ok(thread.stop(TestTimeout));
    zassert_ok(k_sem_take(&harness.stopping, TestTimeout), "thread should report stopping");
}

ZTEST(thread_suite, test_thread_adjust_priority_updates_value) {
    // note: this doesnt really test whether priority is actually reflected. but i reckon that is a bit overkil to test,
    // since the api backing Thread is actually tested.

    ThreadHarness harness{};
    init_harness(harness);

    auto       delegate = TestThread::Delegate::create<thread_delegate>();
    TestThread thread(delegate, &harness, 3, "thread_priority");

    zassert_equal(thread.priority(), 3, "initial priority should match constructor");

    zassert_ok(thread.start());
    zassert_ok(k_sem_take(&harness.started, TestTimeout), "thread should report starting");
    zassert_ok(k_sem_take(&harness.running, TestTimeout), "thread should report running");

    thread.adjust_priority(1);
    zassert_equal(thread.priority(), 1, "priority() should reflect updated value");

    zassert_ok(thread.stop(TestTimeout));
    zassert_ok(k_sem_take(&harness.stopping, TestTimeout), "thread should report stopping");
}

ZTEST(thread_suite, test_thread_stop_handles_all_states) {
    ThreadHarness harness{};
    init_harness(harness);

    auto       delegate = TestThread::Delegate::create<thread_delegate>();
    TestThread thread(delegate, &harness, 5, "thread_stop_states");

    // Test IDLE -> STOPPED transition
    zassert_equal(thread.state(), spn::ThreadState::IDLE, "thread should start in IDLE");
    zassert_ok(thread.stop(K_NO_WAIT), "stop() from IDLE should succeed immediately");
    zassert_equal(thread.state(), spn::ThreadState::STOPPED, "thread should be STOPPED after stop() from IDLE");

    // Test idempotent stop() on STOPPED thread
    zassert_ok(thread.stop(K_NO_WAIT), "second stop() should be successful no-op");
    zassert_equal(thread.state(), spn::ThreadState::STOPPED, "thread should remain STOPPED");
}

ZTEST(thread_suite, test_thread_stop_from_paused_state) {
    ThreadHarness harness{};
    init_harness(harness);

    auto       delegate = TestThread::Delegate::create<thread_delegate>();
    TestThread thread(delegate, &harness, 5, "thread_paused_stop");

    zassert_ok(thread.start());
    zassert_ok(k_sem_take(&harness.started, TestTimeout), "thread should start");
    zassert_ok(k_sem_take(&harness.running, TestTimeout), "thread should be running");

    zassert_ok(thread.pause(TestTimeout), "thread should pause successfully");
    zassert_ok(k_sem_take(&harness.pausing, TestTimeout), "thread should report pausing");
    zassert_equal(thread.state(), spn::ThreadState::PAUSED, "thread should be paused");

    // Test stop() from PAUSED state
    zassert_ok(thread.stop(TestTimeout), "stop() from PAUSED should succeed");
    zassert_ok(k_sem_take(&harness.stopping, TestTimeout), "thread should report stopping");
    zassert_equal(thread.state(), spn::ThreadState::STOPPED, "thread should be stopped");
}

ZTEST(thread_suite, test_thread_respects_custom_pacing_strategy) {
    ThreadHarness harness{};
    init_harness(harness);

    StepPacing pacing{};

    auto       delegate = TestThread::Delegate::create<thread_delegate>();
    TestThread thread(delegate, &harness, 5, "thread_step_pacing", pacing);

    zassert_equal(
        &thread.pacing_strategy(),
        static_cast<spn::threading::IPacingStrategy*>(&pacing),
        "thread should use custom pacing"
    );

    zassert_ok(thread.start());
    zassert_ok(k_sem_take(&harness.started, TestTimeout), "thread should report starting");

    zassert_ok(k_sem_take(&pacing.wait_entered_sem, TestTimeout), "pacing should block before release");
    zassert_equal(k_sem_take(&harness.running, K_NO_WAIT), -EBUSY, "delegate should not run before pacing release");

    k_sem_give(&pacing.wait_sem);

    zassert_ok(k_sem_take(&harness.running, TestTimeout), "delegate should run after pacing release");
    zassert_ok(k_sem_take(&pacing.after_sem, TestTimeout), "pacing should record iteration");

    zassert_ok(thread.stop(TestTimeout));
    zassert_ok(k_sem_take(&harness.stopping, TestTimeout), "thread should report stopping");
}

ZTEST(thread_suite, test_thread_stop_during_starting_delegate) {
    ThreadHarness harness{};
    init_harness(harness);

    auto       delegate = TestThread::Delegate::create<thread_delegate_with_starting_delay>();
    TestThread thread(delegate, &harness, 5, "thread_stop_race");

    zassert_ok(thread.start());
    zassert_ok(k_sem_take(&harness.started, TestTimeout), "thread must enter starting");

    zassert_ok(thread.stop(TestTimeout), "stop must succeed");
    zassert_equal(thread.state(), spn::ThreadState::STOPPED, "thread must be stopped");

    zassert_equal(k_sem_take(&harness.running, K_NO_WAIT), -EBUSY, "thread must not reach running");
}

ZTEST(thread_suite, test_thread_pacing_strategy_swap_rejects_during_active_states) {
    ThreadHarness harness{};
    init_harness(harness);

    StepPacing pacing1{};
    StepPacing pacing2{};

    auto       delegate = TestThread::Delegate::create<thread_delegate>();
    TestThread thread(delegate, &harness, 5, "thread_pacing_swap", pacing1);

    zassert_equal(thread.set_pacing_strategy(pacing2), 0, "pacing swap must succeed when idle");

    zassert_ok(thread.start(), "start must succeed");
    zassert_ok(k_sem_take(&harness.started, TestTimeout), "thread must start");

    zassert_equal(thread.set_pacing_strategy(pacing1), -EBUSY, "must reject pacing swap during starting");

    zassert_ok(thread.stop(TestTimeout), "stop must succeed");
    zassert_ok(k_sem_take(&harness.stopping, TestTimeout), "thread must stop");

    zassert_ok(thread.set_pacing_strategy(pacing1), "pacing swap must succeed when stopped");
}

ZTEST(thread_suite, test_thread_attach_detach_while_idle_and_stopped) {
    ThreadHarness harness{};
    init_harness(harness);

    StepPacing pacing{};

    auto       delegate_obj_a = TestThread::Delegate::create<delegate_a>();
    auto       delegate_obj_b = TestThread::Delegate::create<delegate_b>();
    TestThread thread(delegate_obj_a, &harness, 5, "thread_attach_idle", pacing);

    zassert_true(thread.is_attached(), "delegate should be attached after construction");

    thread.detach();
    zassert_false(thread.is_attached(), "delegate should be detached");

    zassert_ok(thread.attach(delegate_obj_b), "attach while idle must succeed");
    zassert_true(thread.is_attached(), "delegate should be attached");

    zassert_ok(thread.start(), "start must succeed");
    zassert_ok(k_sem_take(&harness.started, TestTimeout), "thread must start");

    zassert_ok(k_sem_take(&pacing.wait_entered_sem, TestTimeout), "pacing must block before release");
    k_sem_give(&pacing.wait_sem);
    zassert_ok(k_sem_take(&harness.running, TestTimeout), "thread must run");
    zassert_ok(k_sem_take(&pacing.after_sem, TestTimeout), "iteration must complete");

    zassert_equal(harness.delegate_b_calls, 1, "delegate b must be invoked");
    zassert_equal(harness.delegate_a_calls, 0, "delegate a must not be invoked");

    zassert_ok(thread.stop(TestTimeout), "stop must succeed");
    zassert_ok(k_sem_take(&harness.stopping, TestTimeout), "thread must stop");

    zassert_ok(thread.attach(delegate_obj_a), "attach while stopped must succeed");
    zassert_true(thread.is_attached(), "delegate should be attached");
}

ZTEST(thread_suite, test_thread_attach_detach_while_running) {
    ThreadHarness harness{};
    init_harness(harness);

    StepPacing pacing{};

    auto       delegate_obj_a = TestThread::Delegate::create<delegate_a>();
    auto       delegate_obj_b = TestThread::Delegate::create<delegate_b>();
    TestThread thread(delegate_obj_a, &harness, 5, "thread_attach_running", pacing);

    zassert_ok(thread.start(), "start must succeed");
    zassert_ok(k_sem_take(&harness.started, TestTimeout), "thread must start");

    // allow first RUNNING call with delegate A
    zassert_ok(k_sem_take(&pacing.wait_entered_sem, TestTimeout), "pacing must block");
    k_sem_give(&pacing.wait_sem);
    zassert_ok(k_sem_take(&harness.running, TestTimeout), "delegate a must run");
    zassert_ok(k_sem_take(&pacing.after_sem, TestTimeout), "iteration must complete");
    zassert_equal(harness.delegate_a_calls, 1, "delegate a must be invoked once");

    // hotswap to delegate B while running
    zassert_ok(k_sem_take(&pacing.wait_entered_sem, TestTimeout), "pacing must block");
    zassert_ok(thread.attach(delegate_obj_b), "hot-swap must succeed");
    k_sem_give(&pacing.wait_sem);
    zassert_ok(k_sem_take(&pacing.after_sem, TestTimeout), "iteration must complete");
    zassert_equal(harness.delegate_b_calls, 1, "delegate b must be invoked");
    zassert_equal(harness.delegate_a_calls, 1, "delegate a call count must not increase");

    // detach delegate - invocations should be skipped
    zassert_ok(k_sem_take(&pacing.wait_entered_sem, TestTimeout), "pacing must block");
    thread.detach();
    zassert_false(thread.is_attached(), "delegate should be detached");
    k_sem_give(&pacing.wait_sem);
    zassert_ok(k_sem_take(&pacing.after_sem, TestTimeout), "iteration must complete even when detached");
    zassert_equal(k_sem_take(&harness.running, K_NO_WAIT), -EBUSY, "no delegate must be invoked");

    // reattach delegate A
    zassert_ok(k_sem_take(&pacing.wait_entered_sem, TestTimeout), "pacing must block");
    zassert_ok(thread.attach(delegate_obj_a), "re-attach must succeed");
    k_sem_give(&pacing.wait_sem);
    zassert_ok(k_sem_take(&pacing.after_sem, TestTimeout), "iteration must complete");
    zassert_equal(harness.delegate_a_calls, 2, "delegate a must be invoked twice total");

    zassert_ok(thread.stop(TestTimeout), "stop must succeed");
    zassert_ok(k_sem_take(&harness.stopping, TestTimeout), "thread must stop");
}

ZTEST(thread_suite, test_pacing_lifecycle_enter_exit_balanced) {
    ThreadHarness harness{};
    init_harness(harness);
    StepPacing pacing{};

    auto       delegate = TestThread::Delegate::create<thread_delegate>();
    TestThread thread(delegate, &harness, 5, "pacing_lifecycle", pacing);

    zassert_equal(pacing.enter_count, 0, "must not call enter before start");
    zassert_equal(pacing.exit_count, 0, "must not call exit before start");

    // start -> run -> pause cycle
    zassert_ok(thread.start());
    zassert_ok(k_sem_take(&harness.started, TestTimeout), "must start");
    zassert_ok(k_sem_take(&pacing.wait_entered_sem, TestTimeout), "must block in pacing wait");
    zassert_equal(pacing.enter_count, 1, "must call enter once on start");
    k_sem_give(&pacing.wait_sem);
    zassert_ok(k_sem_take(&harness.running, TestTimeout), "must run");

    zassert_ok(thread.pause(TestTimeout));
    zassert_ok(k_sem_take(&harness.pausing, TestTimeout), "must pause");
    zassert_equal(pacing.exit_count, 1, "must call exit once on pause");

    // resume -> run -> stop cycle
    zassert_ok(thread.resume());
    zassert_ok(k_sem_take(&harness.resuming, TestTimeout), "must resume");
    zassert_ok(k_sem_take(&pacing.wait_entered_sem, TestTimeout), "must block in pacing wait after resume");
    zassert_equal(pacing.enter_count, 2, "must call enter again on resume");
    k_sem_give(&pacing.wait_sem);
    zassert_ok(k_sem_take(&harness.running_after_resume, TestTimeout), "must run after resume");

    zassert_ok(thread.stop(TestTimeout));
    zassert_ok(k_sem_take(&harness.stopping, TestTimeout), "must stop");
    zassert_equal(pacing.exit_count, 2, "must call exit once on stop");
}
