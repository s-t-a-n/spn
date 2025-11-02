#include "spn/containers/callback.hpp"
#include "spn/threading/thread.hpp"

#include <etl/atomic.h>
#include <zephyr/ztest.h>

using namespace spn;

namespace {

constexpr size_t THREAD_STACK_SIZE = 1024 + CONFIG_TEST_EXTRA_STACK_SIZE;

etl::atomic<int> counted_fn_ctr{0};

void counted_void_fn(int value) { counted_fn_ctr.fetch_add(value, etl::memory_order_seq_cst); }

int counted_int_fn(float value) {
    int result = static_cast<int>(value * 10.0f);
    counted_fn_ctr.fetch_add(1, etl::memory_order_seq_cst);
    return result;
}

struct HotSwapContext {
    callback<void, HotSwapContext*>* cb = nullptr;
    k_sem                            entered{};
    k_sem                            release{};
    etl::atomic<int>                 blocking_calls{0};
    etl::atomic<int>                 non_blocking_calls{0};
};

void hotswap_blocking_fn(HotSwapContext* ctx) {
    ctx->blocking_calls.fetch_add(1, etl::memory_order_seq_cst);
    k_sem_give(&ctx->entered);
    k_sem_take(&ctx->release, K_FOREVER);
}

void hotswap_non_blocking_fn(HotSwapContext* ctx) { ctx->non_blocking_calls.fetch_add(1, etl::memory_order_seq_cst); }

} // namespace

static void test_teardown(void*) { counted_fn_ctr.store(0, etl::memory_order_seq_cst); }

ZTEST_SUITE(callback_suite, NULL, NULL, NULL, test_teardown, NULL);

ZTEST(callback_suite, test_basic_lifecycle_and_errors) {
    callback<void, int> cb_void;
    zassert_false(cb_void.is_attached(), "must start detached");

    // invoke before attach
    auto result_before_attach = cb_void.invoke(1);
    zassert_false(result_before_attach.has_value(), "must fail when never attached");
    zassert_equal(-ENOENT, result_before_attach.error(), "must return -ENOENT when no function attached");

    // void return type
    counted_fn_ctr.store(0, etl::memory_order_seq_cst);
    cb_void.attach(counted_void_fn);
    zassert_true(cb_void.is_attached());

    auto result_void = cb_void.invoke(5);
    zassert_true(result_void.has_value());
    zassert_equal(5, counted_fn_ctr.load(etl::memory_order_seq_cst), "must have run callback with argument");

    cb_void.detach();
    zassert_false(cb_void.is_attached());

    auto result_denied = cb_void.invoke(10);
    zassert_false(result_denied.has_value(), "must fail when detached");
    zassert_equal(-EAGAIN, result_denied.error(), "must return -EAGAIN when detached");

    cb_void.attach(nullptr);
    zassert_false(cb_void.is_attached());

    auto result_nullptr = cb_void.invoke(15);
    zassert_false(result_nullptr.has_value());
    zassert_equal(-ENOENT, result_nullptr.error(), "must return -ENOENT for nullptr not -EAGAIN");

    callback<int, float> cb_int;
    counted_fn_ctr.store(0, etl::memory_order_seq_cst);
    cb_int.attach(counted_int_fn);

    auto result_int = cb_int.invoke(4.2f);
    zassert_true(result_int.has_value());
    zassert_equal(42, result_int.value());
    zassert_equal(1, counted_fn_ctr.load(etl::memory_order_seq_cst));

    cb_int.detach();
}

ZTEST(callback_suite, test_hot_swap) {
    HotSwapContext ctx;
    k_sem_init(&ctx.entered, 0, 1);
    k_sem_init(&ctx.release, 0, 1);

    callback<void, HotSwapContext*> cb;
    cb.attach(hotswap_blocking_fn);
    ctx.cb = &cb;

    auto invoker_entry = [](HotSwapContext* c, ThreadState state) {
        if (state == ThreadState::STARTING) {
            c->cb->invoke(c);
            return;
        }
        k_sleep(K_TICKS(1));
    };

    using TestThread    = Thread<THREAD_STACK_SIZE, HotSwapContext>;
    auto       delegate = TestThread::Delegate::create(invoker_entry);
    TestThread thread(delegate, &ctx, K_PRIO_PREEMPT(5));
    zassert_ok(thread.start());

    zassert_ok(k_sem_take(&ctx.entered, K_SECONDS(1)));

    cb.attach(hotswap_non_blocking_fn);

    k_sem_give(&ctx.release);

    zassert_equal(1, ctx.blocking_calls.load(etl::memory_order_seq_cst), "must have completed old callback");
    zassert_equal(
        0,
        ctx.non_blocking_calls.load(etl::memory_order_seq_cst),
        "must not have called swapped callback yet"
    );

    zassert_true(cb.invoke(&ctx).has_value());
    zassert_equal(1, ctx.non_blocking_calls.load(etl::memory_order_seq_cst), "must use swapped callback");

    cb.detach();
}

ZTEST(callback_suite, test_detach_and_join_semantics) {
    HotSwapContext ctx;
    k_sem_init(&ctx.entered, 0, 1);
    k_sem_init(&ctx.release, 0, 1);

    callback<void, HotSwapContext*> cb;
    cb.attach(hotswap_blocking_fn);

    struct ThreadContext {
        callback<void, HotSwapContext*>* cb        = nullptr;
        HotSwapContext*                  block_ctx = nullptr;
    } thread_ctx;
    thread_ctx.cb        = &cb;
    thread_ctx.block_ctx = &ctx;

    auto invoker_entry = [](ThreadContext* tc, ThreadState state) {
        if (state == ThreadState::STARTING) {
            tc->cb->invoke(tc->block_ctx);
            return;
        }
        k_sleep(K_TICKS(1));
    };

    using TestThread    = Thread<THREAD_STACK_SIZE, ThreadContext>;
    auto       delegate = TestThread::Delegate::create(invoker_entry);
    TestThread thread(delegate, &thread_ctx, K_PRIO_PREEMPT(5));
    zassert_ok(thread.start());

    zassert_ok(k_sem_take(&ctx.entered, K_SECONDS(1)));

    // detach is instant, doesn't wait
    cb.detach();
    zassert_false(cb.is_attached());

    // join with short timeout fails while invoke is blocking
    zassert_equal(-ETIMEDOUT, cb.join(K_MSEC(50)), "must block waiting for active invocations");

    // release and join succeeds
    k_sem_give(&ctx.release);
    zassert_ok(cb.join(K_SECONDS(1)));

    // join with K_NO_WAIT when idle returns immediately
    zassert_ok(cb.join(K_NO_WAIT));
}

ZTEST(callback_suite, test_destructor_waits) {
    HotSwapContext ctx;
    k_sem_init(&ctx.entered, 0, 1);
    k_sem_init(&ctx.release, 0, 1);

    auto* cb = new callback<void, HotSwapContext*>;
    cb->attach(hotswap_blocking_fn);

    struct ThreadContext {
        callback<void, HotSwapContext*>* cb        = nullptr;
        HotSwapContext*                  block_ctx = nullptr;
    } thread_ctx;
    thread_ctx.cb        = cb;
    thread_ctx.block_ctx = &ctx;

    auto invoker_entry = [](ThreadContext* tc, ThreadState state) {
        if (state == ThreadState::STARTING) {
            tc->cb->invoke(tc->block_ctx);
            return;
        }
        k_sleep(K_TICKS(1));
    };

    using InvokerThread            = Thread<THREAD_STACK_SIZE, ThreadContext>;
    auto          invoker_delegate = InvokerThread::Delegate::create(invoker_entry);
    InvokerThread thread1(invoker_delegate, &thread_ctx, K_PRIO_PREEMPT(5));
    zassert_ok(thread1.start());

    zassert_ok(k_sem_take(&ctx.entered, K_SECONDS(1)));

    struct DeleterContext {
        callback<void, HotSwapContext*>** cb_ptr = nullptr;
    } deleter_ctx;
    deleter_ctx.cb_ptr = &cb;

    auto deleter_entry = [](DeleterContext* dc, ThreadState state) {
        if (state == ThreadState::STARTING) {
            delete *dc->cb_ptr;
            return;
        }
        k_sleep(K_TICKS(1));
    };

    using DeleterThread            = Thread<THREAD_STACK_SIZE, DeleterContext>;
    auto          deleter_delegate = DeleterThread::Delegate::create(deleter_entry);
    DeleterThread thread2(deleter_delegate, &deleter_ctx, K_PRIO_PREEMPT(5));
    zassert_ok(thread2.start());

    k_msleep(50);
    k_sem_give(&ctx.release);

    zassert_ok(thread1.stop(K_SECONDS(3)));
    zassert_ok(thread2.stop(K_SECONDS(3)));
    zassert_equal(1, ctx.blocking_calls.load(etl::memory_order_seq_cst), "must block in destructor until complete");
}

ZTEST(callback_suite, test_isr_invocation) {
    callback<void, int> cb;
    cb.attach(counted_void_fn);

    struct TimerContext {
        callback<void, int>* cb = nullptr;
        etl::atomic<int>     isr_calls{0};
    } timer_ctx;
    timer_ctx.cb = &cb;

    auto timer_expiry = [](k_timer* timer) {
        auto* ctx    = static_cast<TimerContext*>(timer->user_data);
        auto  result = ctx->cb->invoke(42);
        if (result.has_value()) {
            ctx->isr_calls.fetch_add(1, etl::memory_order_seq_cst);
        }
    };

    k_timer timer;
    k_timer_init(&timer, timer_expiry, nullptr);
    timer.user_data = &timer_ctx;

    counted_fn_ctr.store(0, etl::memory_order_seq_cst);

    k_timer_start(&timer, K_MSEC(10), K_MSEC(10));
    k_msleep(100);
    k_timer_stop(&timer);

    int isr_calls = timer_ctx.isr_calls.load(etl::memory_order_seq_cst);
    zassert_true(isr_calls > 0, "must be callable from isr");
    zassert_equal(42 * isr_calls, counted_fn_ctr.load(etl::memory_order_seq_cst));

    cb.detach();
}

ZTEST(callback_suite, test_concurrent_invocations) {
    HotSwapContext ctx;
    k_sem_init(&ctx.entered, 0, 2);
    k_sem_init(&ctx.release, 0, 2);

    callback<void, HotSwapContext*> cb;
    cb.attach(hotswap_blocking_fn);

    struct ThreadContext {
        callback<void, HotSwapContext*>* cb   = nullptr;
        HotSwapContext*                  ctx  = nullptr;
        k_sem*                           done = nullptr;
    } thread_ctx;
    thread_ctx.cb  = &cb;
    thread_ctx.ctx = &ctx;

    k_sem work_done;
    k_sem_init(&work_done, 0, 2);
    thread_ctx.done = &work_done;

    auto invoker_entry = [](ThreadContext* tc, ThreadState state) {
        if (state == ThreadState::STARTING) {
            tc->cb->invoke(tc->ctx);
            return;
        }
        k_sem_give(tc->done);
        k_sleep(K_TICKS(1));
    };

    using TestThread    = Thread<THREAD_STACK_SIZE, ThreadContext>;
    auto       delegate = TestThread::Delegate::create(invoker_entry);
    TestThread thread1(delegate, &thread_ctx, K_PRIO_PREEMPT(5));
    TestThread thread2(delegate, &thread_ctx, K_PRIO_PREEMPT(5));
    zassert_ok(thread1.start());
    zassert_ok(thread2.start());

    zassert_ok(k_sem_take(&ctx.entered, K_SECONDS(1)));
    zassert_ok(k_sem_take(&ctx.entered, K_SECONDS(1)));

    k_sem_give(&ctx.release);
    k_sem_give(&ctx.release);

    zassert_ok(k_sem_take(&work_done, K_SECONDS(1)));
    zassert_ok(k_sem_take(&work_done, K_SECONDS(1)));

    zassert_ok(thread1.stop(K_SECONDS(3)));
    zassert_ok(thread2.stop(K_SECONDS(3)));

    zassert_equal(2, ctx.blocking_calls.load(etl::memory_order_seq_cst), "must allow concurrent invocations");

    cb.detach();
}
