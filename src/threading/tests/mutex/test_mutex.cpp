#include "spn/threading/mutex.hpp"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

namespace {

constexpr int  ThreadStackSize = 1024;
constexpr auto TestTimeout     = K_MSEC(250);

K_THREAD_STACK_DEFINE(helper_stack, ThreadStackSize);
k_thread helper_thread;

struct TestSample {
    int value = -1;
};

struct WaiterContext {
    spn::Mutex* mutex         = nullptr;
    int         first_try     = -1;
    int         second_try    = -1;
    int         second_unlock = -1;
    k_sem       first_done;
    k_sem       release;
    k_sem       finished;
};

void waiter_entry(void* context_ptr, void*, void*) {
    auto* ctx = static_cast<WaiterContext*>(context_ptr);

    ctx->first_try = ctx->mutex->lock(K_NO_WAIT);
    k_sem_give(&ctx->first_done);

    k_sem_take(&ctx->release, K_FOREVER);

    ctx->second_try = ctx->mutex->lock(K_FOREVER);
    if (ctx->second_try == 0) {
        ctx->second_unlock = ctx->mutex->unlock();
    }

    k_sem_give(&ctx->finished);
}

struct WithLockTimeoutContext {
    spn::Mutex* mutex               = nullptr;
    bool        int_callable_ran    = false;
    bool        struct_callable_ran = false;
    int         int_result          = 0;
    TestSample  struct_result;
    k_sem       done;
};

void with_lock_timeout_entry(void* context_ptr, void*, void*) {
    auto* ctx = static_cast<WithLockTimeoutContext*>(context_ptr);

    ctx->int_result = ctx->mutex->with_lock(
        [ctx]() {
            ctx->int_callable_ran = true;
            return 123;
        },
        K_NO_WAIT
    );

    ctx->struct_result = ctx->mutex->with_lock(
        [ctx]() {
            ctx->struct_callable_ran = true;
            TestSample sample;
            sample.value = 321;
            return sample;
        },
        K_NO_WAIT
    );

    k_sem_give(&ctx->done);
}

} // namespace

static void test_teardown(void* fixture) {
    if (helper_thread.base.thread_state != 0) {
        k_thread_join(&helper_thread, K_MSEC(100));
        // abort thread unconditionally if something goes haywire
        k_thread_abort(&helper_thread);
    }
}

ZTEST_SUITE(mutex_suite, NULL, NULL, NULL, test_teardown, NULL);

ZTEST(mutex_suite, mutex_reentrant_locking) {
    spn::Mutex mutex;

    zassert_ok(mutex.lock(K_NO_WAIT), "first lock should succeed");
    zassert_ok(mutex.lock(K_NO_WAIT), "reentrant lock should succeed");
    zassert_ok(mutex.unlock(), "first unlock should succeed");
    zassert_ok(mutex.unlock(), "second unlock should succeed");
    zassert_not_equal(mutex.unlock(), 0, "extra unlock should fail");
}

ZTEST(mutex_suite, mutex_with_lock_runs_callable) {
    spn::Mutex mutex;
    auto       call_count = 0;

    const auto result = mutex.with_lock(
        [&call_count]() {
            ++call_count;
            return 42;
        },
        K_FOREVER
    );

    zassert_equal(call_count, 1, "callable should run once");
    zassert_equal(result, 42, "callable result should propagate");
}

ZTEST(mutex_suite, mutex_with_lock_returns_defaults_on_timeout) {
    spn::Mutex mutex;
    zassert_ok(mutex.lock(K_NO_WAIT), "primary thread acquires mutex");

    WithLockTimeoutContext context{};
    context.mutex               = &mutex;
    context.int_result          = 99;
    context.struct_result.value = 88;
    zassert_ok(k_sem_init(&context.done, 0, 1), "completion semaphore should init");

    k_tid_t tid = k_thread_create(
        &helper_thread,
        helper_stack,
        K_THREAD_STACK_SIZEOF(helper_stack),
        with_lock_timeout_entry,
        &context,
        nullptr,
        nullptr,
        K_PRIO_PREEMPT(0),
        0,
        K_NO_WAIT
    );
    zassert_not_null(tid, "timeout thread should start");

    zassert_ok(k_sem_take(&context.done, TestTimeout), "timeout thread should signal");

    zassert_false(context.int_callable_ran, "int callable should not run");
    zassert_equal(context.int_result, 0, "int result should fall back to default");
    zassert_false(context.struct_callable_ran, "struct callable should not run");
    zassert_equal(context.struct_result.value, -1, "struct result should remain default");

    zassert_ok(mutex.unlock(), "primary thread releases mutex");
}

ZTEST(mutex_suite, mutex_lockguard_releases_mutex) {
    spn::Mutex mutex;

    WaiterContext waiter{};
    waiter.mutex = &mutex;
    zassert_ok(k_sem_init(&waiter.first_done, 0, 1), "first semaphore should init");
    zassert_ok(k_sem_init(&waiter.release, 0, 1), "release semaphore should init");
    zassert_ok(k_sem_init(&waiter.finished, 0, 1), "finished semaphore should init");

    {
        auto guard = mutex.lockguard();
        zassert_true(guard.owns_lock(), "guard should own mutex");

        k_tid_t tid = k_thread_create(
            &helper_thread,
            helper_stack,
            K_THREAD_STACK_SIZEOF(helper_stack),
            waiter_entry,
            &waiter,
            nullptr,
            nullptr,
            K_PRIO_PREEMPT(0),
            0,
            K_NO_WAIT
        );
        zassert_not_null(tid, "waiter thread should start");

        zassert_ok(k_sem_take(&waiter.first_done, TestTimeout), "waiter should attempt immediate lock");
        zassert_equal(waiter.first_try, -EBUSY, "mutex should be busy for other threads");

        k_sem_give(&waiter.release);
    }

    zassert_ok(k_sem_take(&waiter.finished, TestTimeout), "waiter should complete after guard");
    zassert_equal(waiter.second_try, 0, "waiter should lock once guard releases");
    zassert_equal(waiter.second_unlock, 0, "waiter unlock should succeed");
}

ZTEST(mutex_suite, mutex_deferred_lockguard_controls_locking) {
    spn::Mutex mutex;

    {
        auto guard = mutex.deferred_lockguard();
        zassert_false(guard.owns_lock(), "deferred guard should start unlocked");
        zassert_ok(guard.lock(K_NO_WAIT), "deferred guard should lock on demand");
        zassert_true(guard.owns_lock(), "deferred guard should report ownership");
        zassert_equal(guard.lock(K_NO_WAIT), -EINVAL, "double lock should fail");
        zassert_equal(guard.unlock(), 0, "deferred guard unlock should succeed");
        zassert_false(guard.owns_lock(), "deferred guard should release ownership");
        zassert_equal(guard.unlock(), -EPERM, "unlock without ownership should fail");
    }

    zassert_ok(mutex.lock(K_NO_WAIT), "mutex should be free after deferred guard");
    zassert_ok(mutex.unlock(), "cleanup unlock should succeed");
}

ZTEST(mutex_suite, mutex_adopted_lockguard_releases_on_scope_exit) {
    spn::Mutex mutex;
    zassert_ok(mutex.lock(K_NO_WAIT), "pre-lock mutex for adopted guard");

    {
        auto guard = mutex.adopted_lockguard();
        zassert_true(guard.owns_lock(), "adopted guard should take ownership");
        zassert_equal(guard.lock(K_NO_WAIT), -EINVAL, "adopted guard should reject extra lock");
        zassert_ok(mutex.lock(K_NO_WAIT), "reentrant lock should still succeed");
        zassert_ok(mutex.unlock(), "reentrant unlock should restore depth");
    }

    zassert_ok(mutex.lock(K_NO_WAIT), "guard destruction should release mutex");
    zassert_ok(mutex.unlock(), "cleanup unlock should succeed");
}

ZTEST(mutex_suite, mutex_thread_contention_blocks_until_release) {
    spn::Mutex mutex;

    WaiterContext waiter{};
    waiter.mutex = &mutex;
    zassert_ok(k_sem_init(&waiter.first_done, 0, 1), "first semaphore should init");
    zassert_ok(k_sem_init(&waiter.release, 0, 1), "release semaphore should init");
    zassert_ok(k_sem_init(&waiter.finished, 0, 1), "finished semaphore should init");

    zassert_ok(mutex.lock(K_NO_WAIT), "primary thread acquires mutex");

    k_tid_t tid = k_thread_create(
        &helper_thread,
        helper_stack,
        K_THREAD_STACK_SIZEOF(helper_stack),
        waiter_entry,
        &waiter,
        nullptr,
        nullptr,
        K_PRIO_PREEMPT(0),
        0,
        K_NO_WAIT
    );
    zassert_not_null(tid, "waiter thread should start");

    zassert_ok(k_sem_take(&waiter.first_done, TestTimeout), "waiter should attempt first lock");
    zassert_equal(waiter.first_try, -EBUSY, "first try should fail while held");

    k_sem_give(&waiter.release);
    zassert_ok(mutex.unlock(), "primary thread releases mutex");

    zassert_ok(k_sem_take(&waiter.finished, TestTimeout), "waiter should finish second attempt");
    zassert_equal(waiter.second_try, 0, "waiter should acquire mutex after release");
    zassert_equal(waiter.second_unlock, 0, "waiter unlock should succeed");
}
