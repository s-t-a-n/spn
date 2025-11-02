#include "spn/containers/callback_delegate.hpp"
#include "spn/threading/thread.hpp"

#include <etl/atomic.h>
#include <zephyr/ztest.h>

using namespace spn;

namespace {

constexpr size_t THREAD_STACK_SIZE = 1024 + CONFIG_TEST_EXTRA_STACK_SIZE;

etl::atomic<int> counted_fn_ctr{0};

void counted_void_fn(int value) { counted_fn_ctr.fetch_add(value, etl::memory_order_seq_cst); }

struct Processor {
    int total = 0;
    int add(int value) {
        total += value;
        return total;
    }
};

struct HotSwapContext {
    callback_delegate<void, HotSwapContext*>* cb = nullptr;
    k_sem                                     entered{};
    k_sem                                     release{};
    etl::atomic<int>                          blocking_calls{0};
    etl::atomic<int>                          non_blocking_calls{0};
};

void hotswap_blocking_fn(HotSwapContext* ctx) {
    ctx->blocking_calls.fetch_add(1, etl::memory_order_seq_cst);
    k_sem_give(&ctx->entered);
    k_sem_take(&ctx->release, K_FOREVER);
}

void hotswap_non_blocking_fn(HotSwapContext* ctx) { ctx->non_blocking_calls.fetch_add(1, etl::memory_order_seq_cst); }

} // namespace

static void test_teardown(void*) { counted_fn_ctr.store(0, etl::memory_order_seq_cst); }

ZTEST_SUITE(callback_delegate_suite, NULL, NULL, NULL, test_teardown, NULL);

ZTEST(callback_delegate_suite, test_basic_lifecycle_and_errors) {
    callback_delegate<void, int> cb_void;
    zassert_false(cb_void.is_attached(), "must start detached");

    // invoke before attach returns -EAGAIN (guard denies acquisitions)
    auto result_before_attach = cb_void.invoke(1);
    zassert_false(result_before_attach.has_value(), "must fail when never attached");
    zassert_equal(-EAGAIN, result_before_attach.error(), "must return -EAGAIN when detached");

    // void return type
    counted_fn_ctr.store(0, etl::memory_order_seq_cst);
    zassert_ok(cb_void.attach(etl::delegate<void(int)>::create<counted_void_fn>()));
    zassert_true(cb_void.is_attached());

    auto result_void = cb_void.invoke(5);
    zassert_true(result_void.has_value());
    zassert_equal(5, counted_fn_ctr.load(etl::memory_order_seq_cst), "must execute callback");

    cb_void.detach();
    zassert_false(cb_void.is_attached());

    auto result_denied = cb_void.invoke(10);
    zassert_false(result_denied.has_value(), "must fail when detached");
    zassert_equal(-EAGAIN, result_denied.error(), "must return -EAGAIN when detached");

    // member function delegate with non-void return
    Processor                   processor{};
    callback_delegate<int, int> cb_member;
    zassert_ok(cb_member.attach(etl::delegate<int(int)>::create<Processor, &Processor::add>(processor)));
    auto result_member = cb_member.invoke(10);
    zassert_true(result_member.has_value());
    zassert_equal(10, result_member.value(), "must return accumulated value");
    zassert_equal(10, processor.total, "must modify instance");
    result_member = cb_member.invoke(32);
    zassert_equal(42, result_member.value(), "must return new total");
    cb_member.detach();

    // invalid delegate rejection
    callback_delegate<void, int> cb_invalid;
    zassert_equal(-EINVAL, cb_invalid.attach(etl::delegate<void(int)>{}), "must reject invalid delegate");
    zassert_false(cb_invalid.is_attached(), "must remain detached");
}

ZTEST(callback_delegate_suite, test_hot_swap) {
    HotSwapContext ctx;
    k_sem_init(&ctx.entered, 0, 1);
    k_sem_init(&ctx.release, 0, 1);

    callback_delegate<void, HotSwapContext*> cb;
    zassert_ok(cb.attach(etl::delegate<void(HotSwapContext*)>::create<hotswap_blocking_fn>()));
    ctx.cb = &cb;

    auto thread_entry = [](HotSwapContext* c, ThreadState state) {
        if (state == ThreadState::STARTING) {
            (void)c->cb->invoke(c);
            return;
        }
        k_sleep(K_TICKS(1));
    };

    Thread<THREAD_STACK_SIZE, HotSwapContext> thread{
        etl::delegate<void(HotSwapContext*, ThreadState)>::create(thread_entry),
        &ctx
    };
    zassert_ok(thread.start());

    zassert_ok(k_sem_take(&ctx.entered, K_SECONDS(1)), "must enter first callback");

    // swap to second delegate while first is running
    zassert_ok(cb.attach(etl::delegate<void(HotSwapContext*)>::create<hotswap_non_blocking_fn>()));

    k_sem_give(&ctx.release);
    zassert_ok(thread.stop(K_SECONDS(3)));

    zassert_equal(1, ctx.blocking_calls.load(etl::memory_order_seq_cst), "must complete first callback");
    zassert_equal(0, ctx.non_blocking_calls.load(etl::memory_order_seq_cst), "must not have called second yet");

    (void)cb.invoke(&ctx);
    zassert_equal(1, ctx.non_blocking_calls.load(etl::memory_order_seq_cst), "must use new callback");

    cb.detach();
}

ZTEST(callback_delegate_suite, test_detach_and_join_semantics) {
    HotSwapContext ctx;
    k_sem_init(&ctx.entered, 0, 1);
    k_sem_init(&ctx.release, 0, 1);

    callback_delegate<void, HotSwapContext*> cb;
    zassert_ok(cb.attach(etl::delegate<void(HotSwapContext*)>::create<hotswap_blocking_fn>()));

    struct ThreadContext {
        callback_delegate<void, HotSwapContext*>* cb        = nullptr;
        HotSwapContext*                           block_ctx = nullptr;
    } thread_ctx;
    thread_ctx.cb        = &cb;
    thread_ctx.block_ctx = &ctx;

    auto invoker_entry = [](ThreadContext* tc, ThreadState state) {
        if (state == ThreadState::STARTING) {
            (void)tc->cb->invoke(tc->block_ctx);
            return;
        }
        k_sleep(K_TICKS(1));
    };

    Thread<THREAD_STACK_SIZE, ThreadContext> thread{
        etl::delegate<void(ThreadContext*, ThreadState)>::create(invoker_entry),
        &thread_ctx
    };
    zassert_ok(thread.start());

    zassert_ok(k_sem_take(&ctx.entered, K_SECONDS(1)), "must enter callback");

    // detach is instant, doesn't wait for active invocations
    cb.detach();
    zassert_false(cb.is_attached());

    // join with short timeout fails while invoke is blocking
    zassert_equal(-ETIMEDOUT, cb.join(K_MSEC(50)), "must timeout while blocked");

    // release and join succeeds
    k_sem_give(&ctx.release);
    zassert_ok(cb.join(K_SECONDS(1)));

    zassert_ok(thread.stop(K_SECONDS(3)));
}

ZTEST(callback_delegate_suite, test_isr_invoke) {
    callback_delegate<void, int> cb;
    zassert_ok(cb.attach(etl::delegate<void(int)>::create<counted_void_fn>()));

    struct TimerContext {
        callback_delegate<void, int>* cb = nullptr;
        etl::atomic<int>              isr_calls{0};
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
    zassert_true(isr_calls > 0, "must invoke from ISR");
    zassert_equal(42 * isr_calls, counted_fn_ctr.load(etl::memory_order_seq_cst), "must execute from ISR");

    cb.detach();

    // attach with K_NO_WAIT from ISR context
    struct AttachContext {
        callback_delegate<void, int>* cb = nullptr;
        etl::atomic<int>              attach_rc{-999};
    } attach_ctx;
    attach_ctx.cb = &cb;

    auto attach_timer_expiry = [](k_timer* timer) {
        auto* ctx = static_cast<AttachContext*>(timer->user_data);
        auto  rc  = ctx->cb->attach(etl::delegate<void(int)>::create<counted_void_fn>(), K_NO_WAIT);
        ctx->attach_rc.store(rc, etl::memory_order_release);
    };

    k_timer attach_timer;
    k_timer_init(&attach_timer, attach_timer_expiry, nullptr);
    attach_timer.user_data = &attach_ctx;
    k_timer_start(&attach_timer, K_MSEC(10), K_NO_WAIT);
    k_msleep(30);

    zassert_ok(attach_ctx.attach_rc.load(etl::memory_order_acquire), "must attach from ISR");
    zassert_true(cb.is_attached());

    cb.detach();
}

ZTEST(callback_delegate_suite, test_destructor_blocking) {
    HotSwapContext ctx;
    k_sem_init(&ctx.entered, 0, 1);
    k_sem_init(&ctx.release, 0, 1);

    auto* cb = new callback_delegate<void, HotSwapContext*>; // malloc to allow non-local deletion
    zassert_ok(cb->attach(etl::delegate<void(HotSwapContext*)>::create<hotswap_blocking_fn>()));

    struct ThreadContext {
        callback_delegate<void, HotSwapContext*>** cb_ptr    = nullptr;
        HotSwapContext*                            block_ctx = nullptr;
    } thread_ctx;
    thread_ctx.cb_ptr    = &cb;
    thread_ctx.block_ctx = &ctx;

    auto invoker_entry = [](ThreadContext* tc, ThreadState state) {
        if (state == ThreadState::STARTING) {
            (*tc->cb_ptr)->invoke(tc->block_ctx);
            return;
        }
        k_sleep(K_TICKS(1));
    };

    auto deleter_entry = [](ThreadContext* tc, ThreadState state) {
        if (state == ThreadState::STARTING) {
            delete *tc->cb_ptr;
            return;
        }
        k_sleep(K_TICKS(1));
    };

    auto invoker_delegate = etl::delegate<void(ThreadContext*, ThreadState)>::create(invoker_entry);
    auto deleter_delegate = etl::delegate<void(ThreadContext*, ThreadState)>::create(deleter_entry);
    Thread<THREAD_STACK_SIZE, ThreadContext> invoker{invoker_delegate, &thread_ctx};
    Thread<THREAD_STACK_SIZE, ThreadContext> deleter{deleter_delegate, &thread_ctx};

    zassert_ok(invoker.start());
    zassert_ok(k_sem_take(&ctx.entered, K_SECONDS(1)), "must enter callback");

    zassert_ok(deleter.start());
    k_msleep(50);
    k_sem_give(&ctx.release);

    zassert_ok(invoker.stop(K_SECONDS(3)));
    zassert_ok(deleter.stop(K_SECONDS(3)));
    zassert_equal(1, ctx.blocking_calls.load(etl::memory_order_seq_cst), "must complete invocation");
}

ZTEST(callback_delegate_suite, test_concurrent_invocations) {
    // this is quite a dirty test, as it is non-deterministic. However, given that I have already caught some nasty bugs
    // with it and given how nasty and subtle these bugs can get, we'll leave it in.

    struct StressContext {
        callback_delegate<void, int>* cb = nullptr;
        etl::atomic<int>              iterations{0};
        etl::atomic<int>              invocations{0};
    } ctx;

    auto stress_callback = [](int value) { (void)value; };

    callback_delegate<void, int> cb;
    zassert_ok(cb.attach(etl::delegate<void(int)>::create(stress_callback)));
    ctx.cb = &cb;

    auto invoker_entry = [](StressContext* sc, ThreadState state) {
        if (state != ThreadState::RUNNING) {
            k_sleep(K_TICKS(1));
            return;
        }
        if (sc->iterations.fetch_add(1, etl::memory_order_seq_cst) >= 200) {
            k_sleep(K_TICKS(1));
            return;
        }
        auto result = sc->cb->invoke(42);
        if (result.has_value()) {
            sc->invocations.fetch_add(1, etl::memory_order_seq_cst);
        }
    };

    auto delegate = etl::delegate<void(StressContext*, ThreadState)>::create(invoker_entry);
    Thread<THREAD_STACK_SIZE, StressContext> thread1{delegate, &ctx};
    Thread<THREAD_STACK_SIZE, StressContext> thread2{delegate, &ctx};

    zassert_ok(thread1.start());
    zassert_ok(thread2.start());

    k_msleep(100);

    zassert_ok(thread1.stop(K_SECONDS(3)));
    zassert_ok(thread2.stop(K_SECONDS(3)));

    int total_invocations = ctx.invocations.load(etl::memory_order_seq_cst);
    zassert_true(total_invocations >= 200, "must complete many invocations");

    cb.detach();
}
