#include "spn/threading/thread.hpp"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(thread_suite, NULL, NULL, NULL, NULL, NULL);

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
}

// the reference entry
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

using TestThread = spn::Thread<1024, ThreadHarness>;

} // namespace

ZTEST(thread_suite, thread_start_transitions_to_running) {
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

ZTEST(thread_suite, thread_pause_and_resume_transitions) {
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

ZTEST(thread_suite, thread_start_or_resume_handles_states) {
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

ZTEST(thread_suite, thread_adjust_priority_updates_value) {
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
