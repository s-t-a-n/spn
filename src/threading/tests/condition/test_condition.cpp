#include "spn/threading/condition.hpp"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

namespace {

// for these tests we assume that Zephyr k_condvar/k_mutex behaviour has been properly tested upstream.

K_THREAD_STACK_DEFINE(test_stack1, 1024);
K_THREAD_STACK_DEFINE(test_stack2, 1024);
k_thread test_thread1;
k_thread test_thread2;

struct TestHarness {
    spn::Condition* condition     = nullptr;
    bool*           predicate     = nullptr;
    bool            set_predicate = false;
    k_sem*          started_sem   = nullptr;
    k_sem*          completed_sem = nullptr;
    k_sem*          wait_sem      = nullptr;
    k_sem*          notify_sem    = nullptr;
    int*            result_slot   = nullptr;
};

void signal_worker_entry(void* ctx_ptr, void*, void*) {
    auto* ctx = static_cast<TestHarness*>(ctx_ptr);
    if (ctx->started_sem != nullptr) {
        k_sem_give(ctx->started_sem);
    }
    if (ctx->wait_sem != nullptr) {
        (void)k_sem_take(ctx->wait_sem, K_FOREVER);
    }
    {
        auto guard = ctx->condition->lockguard();
        if (ctx->set_predicate && ctx->predicate != nullptr) {
            *ctx->predicate = true;
        }
        ctx->condition->signal();
    }
    if (ctx->notify_sem != nullptr) {
        k_sem_give(ctx->notify_sem);
    }
    if (ctx->completed_sem != nullptr) {
        k_sem_give(ctx->completed_sem);
    }
}

void lock_attempt_entry(void* ctx_ptr, void*, void*) {
    auto* ctx = static_cast<TestHarness*>(ctx_ptr);
    if (ctx->started_sem != nullptr) {
        k_sem_give(ctx->started_sem);
    }

    int rc = ctx->condition->lock(K_NO_WAIT);
    if (ctx->result_slot != nullptr) {
        *ctx->result_slot = rc;
    }
    if (rc == 0) {
        (void)ctx->condition->unlock();
    }

    if (ctx->completed_sem != nullptr) {
        k_sem_give(ctx->completed_sem);
    }
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

ZTEST(condition_tests, test_lockguard_enforces_mutex_ownership) {
    spn::Condition condition;

    k_sem start_sem;
    k_sem done_sem;
    zassert_ok(k_sem_init(&start_sem, 0, 1), "start semaphore should init");
    zassert_ok(k_sem_init(&done_sem, 0, 1), "done semaphore should init");

    int         lock_result = 0;
    TestHarness harness{
        .condition     = &condition,
        .started_sem   = &start_sem,
        .completed_sem = &done_sem,
        .result_slot   = &lock_result,
    };

    {
        auto guard = condition.lockguard();

        k_tid_t tid = k_thread_create(
            &test_thread1,
            test_stack1,
            K_THREAD_STACK_SIZEOF(test_stack1),
            lock_attempt_entry,
            &harness,
            nullptr,
            nullptr,
            K_PRIO_PREEMPT(1),
            0,
            K_NO_WAIT
        );
        zassert_not_null(tid, "lock attempt worker should start");

        zassert_ok(k_sem_take(&start_sem, K_MSEC(100)), "worker should attempt lock while guard active");
        zassert_ok(k_sem_take(&done_sem, K_MSEC(100)), "worker should finish lock attempt");
        zassert_equal(lock_result, -EBUSY, "lock attempt from another thread should see busy mutex");
    }

    zassert_ok(condition.lock(K_NO_WAIT), "mutex should be reacquirable after guard scope");
    zassert_ok(condition.unlock(), "reacquired mutex should unlock cleanly");
}

ZTEST(condition_tests, test_wait_for_handles_spurious_signal) {
    spn::Condition condition;
    bool           predicate = false;

    k_sem start_sem_spurious;
    k_sem start_sem_fulfill;
    k_sem done_sem_spurious;
    k_sem done_sem_fulfill;
    k_sem gate_sem;

    zassert_ok(k_sem_init(&start_sem_spurious, 0, 1), "start semaphore should init");
    zassert_ok(k_sem_init(&start_sem_fulfill, 0, 1), "start semaphore should init");
    zassert_ok(k_sem_init(&done_sem_spurious, 0, 1), "done semaphore should init");
    zassert_ok(k_sem_init(&done_sem_fulfill, 0, 1), "done semaphore should init");
    zassert_ok(k_sem_init(&gate_sem, 0, 1), "gate semaphore should init");

    TestHarness spurious_ctx{
        .condition     = &condition,
        .predicate     = &predicate,
        .set_predicate = false,
        .started_sem   = &start_sem_spurious,
        .completed_sem = &done_sem_spurious,
        .wait_sem      = nullptr,
        .notify_sem    = &gate_sem,
    };

    TestHarness fulfilling_ctx{
        .condition     = &condition,
        .predicate     = &predicate,
        .set_predicate = true,
        .started_sem   = &start_sem_fulfill,
        .completed_sem = &done_sem_fulfill,
        .wait_sem      = &gate_sem,
        .notify_sem    = nullptr,
    };

    auto guard = condition.lockguard();

    k_tid_t spurious_tid = k_thread_create(
        &test_thread1,
        test_stack1,
        K_THREAD_STACK_SIZEOF(test_stack1),
        signal_worker_entry,
        &spurious_ctx,
        nullptr,
        nullptr,
        K_PRIO_PREEMPT(1),
        0,
        K_NO_WAIT
    );
    zassert_not_null(spurious_tid, "spurious signal worker should start");

    k_tid_t fulfilling_tid = k_thread_create(
        &test_thread2,
        test_stack2,
        K_THREAD_STACK_SIZEOF(test_stack2),
        signal_worker_entry,
        &fulfilling_ctx,
        nullptr,
        nullptr,
        K_PRIO_PREEMPT(1),
        0,
        K_NO_WAIT
    );
    zassert_not_null(fulfilling_tid, "fulfilling worker should start");

    zassert_ok(k_sem_take(&start_sem_spurious, K_MSEC(100)), "spurious worker should reach start barrier");
    zassert_ok(k_sem_take(&start_sem_fulfill, K_MSEC(100)), "fulfilling worker should reach start barrier");

    bool result = condition.wait_for(K_MSEC(200), [&predicate] { return predicate; });
    zassert_true(result, "wait_for should return true once predicate is eventually satisfied");
    zassert_true(predicate, "predicate should be set by fulfilling worker");

    zassert_ok(k_sem_take(&done_sem_spurious, K_MSEC(100)), "spurious worker should complete");
    zassert_ok(k_sem_take(&done_sem_fulfill, K_MSEC(100)), "fulfilling worker should complete");
}
