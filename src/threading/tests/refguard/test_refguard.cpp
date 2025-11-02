#include "spn/threading/refguard.hpp"
#include "spn/threading/thread.hpp"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

namespace {

constexpr size_t THREAD_STACK_SIZE = 1024 + CONFIG_TEST_EXTRA_STACK_SIZE;

struct Context {
    spn::RefGuard* guard           = nullptr;
    bool           acquired        = false;
    int            rc              = -1;
    int            acquire_count   = 0;
    int            exclusive_count = 0;
    k_sem          ready;
    k_sem          release;
    k_sem          go;
    k_sem          done;
};

} // namespace

ZTEST_SUITE(refguard_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(refguard_suite, test_single_threaded_lifecycle) {
    spn::RefGuard guard;

    zassert_true(guard.is_alive());
    zassert_equal(guard.ref_count(), 0);

    zassert_true(guard.try_acquire());
    zassert_equal(guard.ref_count(), 1);

    zassert_true(guard.try_acquire());
    zassert_equal(guard.ref_count(), 2);

    guard.release();
    zassert_equal(guard.ref_count(), 1);

    guard.release();
    zassert_equal(guard.ref_count(), 0, "must return to 0 references");

    guard.deny_acquisitions();
    zassert_false(guard.is_alive());
    zassert_false(guard.try_acquire(), "must reject acquisitions when stopped");

    guard.allow_acquisitions();
    zassert_true(guard.is_alive(), "must restart");
    zassert_true(guard.try_acquire());
    guard.release();

    zassert_true(guard.try_acquire());
    zassert_equal(guard.ref_count(), 1);
    guard.deny_acquisitions();
    zassert_false(guard.is_alive(), "must stop even with outstanding references");
    zassert_false(guard.try_acquire(), "must block new aqcuisitions while stopped");
    guard.release();
    zassert_equal(guard.ref_count(), 0, "must release outstanding references normally");
}

ZTEST(refguard_suite, test_ref_wrapper_raii) {
    spn::RefGuard guard;

    {
        auto ref = guard.try_acquire_scoped();
        zassert_true(ref.is_acquired());
        zassert_true(bool(ref));
        zassert_equal(guard.ref_count(), 1);
    }

    zassert_equal(guard.ref_count(), 0, "must release on destruction");

    auto ref1 = guard.try_acquire_scoped();
    zassert_true(ref1.is_acquired());

    auto ref2 = etl::move(ref1);
    zassert_false(ref1.is_acquired());
    zassert_true(ref2.is_acquired());
    zassert_equal(guard.ref_count(), 1, "must preserve refcount on move");

    auto ref3 = guard.try_acquire_scoped();
    ref3      = etl::move(ref2);
    zassert_false(ref2.is_acquired());
    zassert_true(ref3.is_acquired());
    zassert_equal(guard.ref_count(), 1, "must release old ref");

    ref3 = etl::move(ref3);
    zassert_true(ref3.is_acquired(), "must handle self move");
    zassert_equal(guard.ref_count(), 1, "must preserve refcount on self move");

    guard.deny_acquisitions();
    auto failed_ref = guard.try_acquire_scoped();
    zassert_false(failed_ref.is_acquired());
    zassert_false(bool{failed_ref});

    auto failed_ref2 = etl::move(failed_ref);
    zassert_false(failed_ref.is_acquired());
    zassert_false(failed_ref2.is_acquired());
}

ZTEST(refguard_suite, test_exclusive_mode) {
    spn::RefGuard guard;

    zassert_true(guard.try_acquire_exclusive(), "must acquire when idle");
    zassert_equal(guard.ref_count(), 1);

    zassert_false(guard.try_acquire(), "must block shared while exclusive ref held");
    zassert_false(guard.try_acquire_exclusive(), "must block second exclusive");

    guard.release_exclusive();
    zassert_equal(guard.ref_count(), 0);

    zassert_true(guard.try_acquire(), "must allow shared after exclusive released");
    zassert_false(guard.try_acquire_exclusive(), "must block exclusive while shared ref is held");
    guard.release();

    zassert_true(guard.try_acquire_exclusive());
    guard.release_exclusive();

    Context ctx1{};
    ctx1.guard = &guard;
    k_sem_init(&ctx1.ready, 0, 1);
    k_sem_init(&ctx1.release, 0, 1);
    k_sem_init(&ctx1.done, 0, 1);

    Context ctx2{};
    ctx2.guard = &guard;
    k_sem_init(&ctx2.ready, 0, 1);
    k_sem_init(&ctx2.release, 0, 1);
    k_sem_init(&ctx2.done, 0, 1);

    auto exclusive_contention_entry = [](Context* ctx, spn::ThreadState state) {
        if (state == spn::ThreadState::STARTING) { // run once
            k_sem_give(&ctx->ready);
            ctx->acquired = ctx->guard->try_acquire_exclusive();
            k_sleep(K_MSEC(250)); // actual work
            k_sem_take(&ctx->release, K_SECONDS(1));
            if (ctx->acquired) ctx->guard->release_exclusive();
            return;
        }
        k_sem_give(&ctx->done);
        k_sleep(K_TICKS(1));
    };

    using TestThread    = spn::Thread<THREAD_STACK_SIZE, Context>;
    auto       delegate = TestThread::Delegate::create(exclusive_contention_entry);
    TestThread thread1{delegate, &ctx1};
    TestThread thread2{delegate, &ctx2};

    thread1.start();
    thread2.start();

    zassert_ok(k_sem_take(&ctx1.ready, K_SECONDS(1)));
    zassert_ok(k_sem_take(&ctx2.ready, K_SECONDS(1)));

    k_sem_give(&ctx1.release);
    k_sem_give(&ctx2.release);

    zassert_ok(k_sem_take(&ctx1.done, K_SECONDS(1)));
    zassert_ok(k_sem_take(&ctx2.done, K_SECONDS(1)));

    zassert_true(ctx1.acquired != ctx2.acquired, "must grant exclusive ref to exactly one thread");

    zassert_ok(thread1.stop(K_SECONDS(3)));
    zassert_ok(thread2.stop(K_SECONDS(3)));
}

ZTEST(refguard_suite, test_concurrent_shared_acquires) {
    spn::RefGuard guard;

    Context ctx1{};
    ctx1.guard = &guard;
    k_sem_init(&ctx1.ready, 0, 1);
    k_sem_init(&ctx1.release, 0, 1);
    k_sem_init(&ctx1.done, 0, 1);

    Context ctx2{};
    ctx2.guard = &guard;
    k_sem_init(&ctx2.ready, 0, 1);
    k_sem_init(&ctx2.release, 0, 1);
    k_sem_init(&ctx2.done, 0, 1);

    auto acquire_entry = [](Context* ctx, spn::ThreadState state) {
        if (state == spn::ThreadState::STARTING) { // run once, acquire and release reference
            k_sem_give(&ctx->ready);
            k_sem_take(&ctx->release, K_SECONDS(1));

            ctx->acquired = ctx->guard->try_acquire();
            if (ctx->acquired) ctx->guard->release();

            k_sem_give(&ctx->done);
            return;
        }
        k_sleep(K_TICKS(1));
    };

    using TestThread = spn::Thread<THREAD_STACK_SIZE, Context>;
    auto delegate    = TestThread::Delegate::create(acquire_entry);

    TestThread thread1{delegate, &ctx1};
    TestThread thread2{delegate, &ctx2};

    thread1.start();
    thread2.start();

    zassert_ok(k_sem_take(&ctx1.ready, K_SECONDS(1)));
    zassert_ok(k_sem_take(&ctx2.ready, K_SECONDS(1)));

    k_sem_give(&ctx1.release);
    k_sem_give(&ctx2.release);

    zassert_ok(k_sem_take(&ctx1.done, K_SECONDS(1)));
    zassert_ok(k_sem_take(&ctx2.done, K_SECONDS(1)));

    // both threads must be granted a shared ref
    zassert_true(ctx1.acquired);
    zassert_true(ctx2.acquired);
    zassert_equal(guard.ref_count(), 0);
}

ZTEST(refguard_suite, test_wait_for_release) {
    spn::RefGuard guard;

    zassert_ok(guard.wait_for_release(K_NO_WAIT), "must return immediately when idle");

    zassert_true(guard.try_acquire());
    zassert_equal(guard.ref_count(), 1);

    Context ctx{};
    ctx.guard = &guard;
    k_sem_init(&ctx.ready, 0, 1);
    k_sem_init(&ctx.done, 0, 1);

    auto release_waiter_entry = [](Context* ctx, spn::ThreadState state) {
        if (state == spn::ThreadState::STARTING) {
            k_sem_give(&ctx->ready);
            ctx->rc = ctx->guard->wait_for_release(K_SECONDS(1));
            k_sem_give(&ctx->done);
            return;
        }
        k_sleep(K_TICKS(1));
    };

    using TestThread    = spn::Thread<THREAD_STACK_SIZE, Context>;
    auto       delegate = TestThread::Delegate::create(release_waiter_entry);
    TestThread thread(delegate, &ctx);

    thread.start();

    zassert_ok(k_sem_take(&ctx.ready, K_SECONDS(1)));

    guard.release();
    zassert_ok(k_sem_take(&ctx.done, K_SECONDS(1)), "must unblock waiter");
    zassert_ok(ctx.rc);

    zassert_true(guard.try_acquire());
    int timeout_rc = guard.wait_for_release(K_MSEC(10));
    zassert_equal(timeout_rc, -ETIMEDOUT, "must timeout when refs held");
    guard.release();
}

ZTEST(refguard_suite, test_teardown) {
    spn::RefGuard guard;

    zassert_true(guard.try_acquire());
    zassert_equal(guard.ref_count(), 1);

    Context ctx{};
    ctx.guard = &guard;
    k_sem_init(&ctx.ready, 0, 1);
    k_sem_init(&ctx.done, 0, 1);

    auto teardown_worker_entry = [](Context* ctx, spn::ThreadState state) {
        if (state == spn::ThreadState::STARTING) {
            k_sem_give(&ctx->ready);
            ctx->rc = ctx->guard->teardown(K_SECONDS(1));
            k_sem_give(&ctx->done);
            return;
        }
        k_sleep(K_TICKS(1));
    };

    using TestThread    = spn::Thread<THREAD_STACK_SIZE, Context>;
    auto       delegate = TestThread::Delegate::create(teardown_worker_entry);
    TestThread thread(delegate, &ctx);

    thread.start();

    zassert_ok(k_sem_take(&ctx.ready, K_SECONDS(1)));

    k_sleep(K_MSEC(50));
    zassert_false(guard.is_alive(), "must stop new acquisitions");

    guard.release();
    zassert_ok(k_sem_take(&ctx.done, K_SECONDS(1)), "must unblock after last release");
    zassert_ok(ctx.rc);
    zassert_equal(guard.ref_count(), 0);

    zassert_false(guard.try_acquire(), "must stay stopped");

    spn::RefGuard guard_timeout;
    zassert_true(guard_timeout.try_acquire());
    int timeout_rc = guard_timeout.teardown(K_MSEC(10));
    zassert_equal(timeout_rc, -ETIMEDOUT, "must timeout when refs held");

    {
        spn::RefGuard guard_destructor;
        (void)guard_destructor.teardown(K_SECONDS(1));
    }

    zassert_equal(guard.ref_count(), 0);
    guard.allow_acquisitions();
    zassert_true(guard.try_acquire(), "must allow reactivation");
    zassert_equal(guard.ref_count(), 1);
    guard.release();
}

ZTEST(refguard_suite, test_stress_concurrent_operations) {
    spn::RefGuard guard;

    Context ctx[4];
    for (auto& c : ctx) {
        c.guard = &guard;
        k_sem_init(&c.go, 0, 1);
        k_sem_init(&c.done, 0, 1);
    }

    auto stress_entry = [](Context* ctx, spn::ThreadState state) {
        if (state == spn::ThreadState::STARTING) {
            k_sem_take(&ctx->go, K_SECONDS(1));
            for (int i = 0; i < 50; ++i) {
                if (ctx->guard->try_acquire()) {
                    ctx->acquire_count++;
                    ctx->guard->release();
                }
                if (ctx->guard->try_acquire_exclusive()) {
                    ctx->exclusive_count++;
                    ctx->guard->release_exclusive();
                }
            }
            k_sem_give(&ctx->done);
            return;
        }
        k_sleep(K_TICKS(1));
    };

    using TestThread    = spn::Thread<THREAD_STACK_SIZE, Context>;
    auto       delegate = TestThread::Delegate::create(stress_entry);
    TestThread threads[4]{
        TestThread{delegate, &ctx[0]},
        TestThread{delegate, &ctx[1]},
        TestThread{delegate, &ctx[2]},
        TestThread{delegate, &ctx[3]},
    };

    for (auto& t : threads)
        zassert_ok(t.start());
    for (auto& c : ctx)
        k_sem_give(&c.go);
    for (auto& c : ctx)
        zassert_ok(k_sem_take(&c.done, K_SECONDS(2)));

    int total_ops = 0;
    for (auto& c : ctx)
        total_ops += c.acquire_count + c.exclusive_count;

    zassert_true(total_ops >= 300, "must succeed at least 3/4 of 400 attempts");
    zassert_equal(guard.ref_count(), 0);
    zassert_true(guard.is_alive(), "must remain active after stress");
}

ZTEST(refguard_suite, test_start_not_alive) {
    spn::RefGuard guard{false};

    zassert_false(guard.is_alive(), "must start stopped");
    zassert_equal(guard.ref_count(), 0);
    zassert_false(guard.try_acquire(), "must block shared when stopped");
    zassert_false(guard.try_acquire_exclusive(), "must block exclusive when stopped");

    guard.allow_acquisitions();
    zassert_true(guard.is_alive());
    zassert_true(guard.try_acquire());
    zassert_equal(guard.ref_count(), 1);
    guard.release();
}
