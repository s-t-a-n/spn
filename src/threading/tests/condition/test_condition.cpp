#include "spn/threading/condition.hpp"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

namespace {

K_THREAD_STACK_DEFINE(test_stack1, 1024);
K_THREAD_STACK_DEFINE(test_stack2, 1024);
k_thread test_thread1;
k_thread test_thread2;

struct TestHarness {
    spn::Condition* condition    = nullptr;
    bool*           predicate    = nullptr;
    k_sem*          waiting_sem  = nullptr;
    k_sem*          released_sem = nullptr;
};

void simple_signaling_entry(void* ctx_ptr, void*, void*) {
    auto* harness       = static_cast<TestHarness*>(ctx_ptr);
    auto  guard         = harness->condition->lockguard();
    *harness->predicate = true;
    harness->condition->signal();
}

void broadcast_waiter_entry(void* ctx_ptr, void*, void*) {
    auto* ctx = static_cast<TestHarness*>(ctx_ptr);
    {
        auto guard = ctx->condition->lockguard();
        k_sem_give(ctx->waiting_sem);
        (void)ctx->condition->wait_for(K_MSEC(200), [ctx] { return *ctx->predicate; });
    }
    k_sem_give(ctx->released_sem);
}

} // namespace

static void test_teardown(void*) {
    // abort thread unconditionally if something goes haywire
    if (test_thread1.base.thread_state != 0) {
        (void)k_thread_join(&test_thread1, K_MSEC(100));
        k_thread_abort(&test_thread1);
    }
    if (test_thread2.base.thread_state != 0) {
        (void)k_thread_join(&test_thread2, K_MSEC(100));
        k_thread_abort(&test_thread2);
    }
}

ZTEST_SUITE(condition_tests, nullptr, nullptr, nullptr, test_teardown, nullptr);

ZTEST(condition_tests, test_immediate_predicate_satisfaction) {
    spn::Condition condition;
    bool           flag = true;

    auto guard  = condition.lockguard();
    bool result = condition.wait_for(K_MSEC(100), [&flag] { return flag; });

    zassert_true(result, "Should return true when predicate is already satisfied");
}

ZTEST(condition_tests, test_timeout_semantics) {
    spn::Condition condition;
    bool           flag  = false;
    auto           guard = condition.lockguard();

    bool result = condition.wait_for(K_MSEC(10), [&flag] { return flag; });

    zassert_false(result, "wait_for should return false on timeout");
}

ZTEST(condition_tests, test_simple_signaling) {
    spn::Condition condition;
    bool           worker_completed = false;
    TestHarness    harness{&condition, &worker_completed, nullptr, nullptr};

    k_tid_t tid = k_thread_create(
        &test_thread1,
        test_stack1,
        K_THREAD_STACK_SIZEOF(test_stack1),
        simple_signaling_entry,
        &harness,
        nullptr,
        nullptr,
        K_PRIO_PREEMPT(1),
        0,
        K_NO_WAIT
    );
    zassert_not_null(tid, "worker thread should start");

    {
        auto guard  = condition.lockguard();
        bool result = condition.wait_for(K_MSEC(100), [&worker_completed] { return worker_completed; });
        zassert_true(result, "Should return true when signaled");
    }

    zassert_true(worker_completed, "Flag should be set by worker thread");
}

ZTEST(condition_tests, test_broadcast_wakes_all_waiters) {
    spn::Condition condition{};
    bool           predicate = false;

    k_sem waiting_sem;
    k_sem released_sem;
    zassert_ok(k_sem_init(&waiting_sem, 0, 2), "waiting semaphore should init");
    zassert_ok(k_sem_init(&released_sem, 0, 2), "released semaphore should init");

    TestHarness harness0{&condition, &predicate, &waiting_sem, &released_sem};
    TestHarness harness1{&condition, &predicate, &waiting_sem, &released_sem};

    k_tid_t tid0 = k_thread_create(
        &test_thread1,
        test_stack1,
        K_THREAD_STACK_SIZEOF(test_stack1),
        broadcast_waiter_entry,
        &harness0,
        nullptr,
        nullptr,
        K_PRIO_PREEMPT(1),
        0,
        K_NO_WAIT
    );
    zassert_not_null(tid0, "first waiter thread should have started");

    k_tid_t tid1 = k_thread_create(
        &test_thread2,
        test_stack2,
        K_THREAD_STACK_SIZEOF(test_stack2),
        broadcast_waiter_entry,
        &harness1,
        nullptr,
        nullptr,
        K_PRIO_PREEMPT(1),
        0,
        K_NO_WAIT
    );
    zassert_not_null(tid1, "second waiter thread should have started");

    zassert_ok(k_sem_take(&waiting_sem, K_MSEC(100)), "first waiter should report waiting");
    zassert_ok(k_sem_take(&waiting_sem, K_MSEC(100)), "second waiter should report waiting");

    k_sleep(K_MSEC(100));

    {
        auto guard = condition.lockguard();
        predicate  = true;
        zassert_equal(condition.broadcast(), 2, "broadcast should awake 2 waiters");
    }

    zassert_ok(k_sem_take(&released_sem, K_MSEC(100)), "first waiter should be released");
    zassert_ok(k_sem_take(&released_sem, K_MSEC(100)), "second waiter should be released");
}
