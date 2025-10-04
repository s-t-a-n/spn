#include "spn/containers/queue/mpmc_deque.hpp"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

using namespace spn;

namespace {
constexpr int    ThreadStackSize = 1024;
constexpr size_t SmallCapacity   = 4;
constexpr size_t MediumCapacity  = 16;

K_THREAD_STACK_DEFINE(producer_stack1, ThreadStackSize);
K_THREAD_STACK_DEFINE(producer_stack2, ThreadStackSize);
K_THREAD_STACK_DEFINE(consumer_stack1, ThreadStackSize);
K_THREAD_STACK_DEFINE(consumer_stack2, ThreadStackSize);
k_thread producer_thread1;
k_thread producer_thread2;
k_thread consumer_thread1;
k_thread consumer_thread2;

struct TestObject {
    int        value;
    static int destructor_count;

    TestObject(int v = 0) : value(v) {}
    ~TestObject() { ++destructor_count; }
};

int TestObject::destructor_count = 0;

struct MoveOnlyObject {
    int value;

    MoveOnlyObject(int v) : value(v) {}
    MoveOnlyObject(const MoveOnlyObject&)            = delete;
    MoveOnlyObject& operator=(const MoveOnlyObject&) = delete;
    MoveOnlyObject(MoveOnlyObject&&)                 = default;
    MoveOnlyObject& operator=(MoveOnlyObject&&)      = default;
};

struct ConstructionCounter {
    int               value;
    static inline int construction_count = 0;

    explicit ConstructionCounter(int v) : value(v) { ++construction_count; }
    ConstructionCounter(const ConstructionCounter& other) : value(other.value) { ++construction_count; }
    ConstructionCounter(ConstructionCounter&& other) : value(other.value) { ++construction_count; }
    ConstructionCounter& operator=(const ConstructionCounter&) = default;
    ConstructionCounter& operator=(ConstructionCounter&&)      = default;

    static void reset() { construction_count = 0; }
};

struct ProducerContext {
    MPMCDeque<int, MediumCapacity>* deque;
    int                             base_value;
    int                             count;
    k_sem*                          done_sem;
};

void producer_back_entry(void* ctx_ptr, void*, void*) {
    auto* ctx = static_cast<ProducerContext*>(ctx_ptr);
    for (int i = 0; i < ctx->count; ++i) {
        ctx->deque->push_back(ctx->base_value + i, K_FOREVER);
    }
    k_sem_give(ctx->done_sem);
}

struct ConsumerContext {
    MPMCDeque<int, MediumCapacity>* deque;
    int*                            values;
    int                             count;
    k_sem*                          done_sem;
};

void consumer_front_entry(void* ctx_ptr, void*, void*) {
    auto* ctx = static_cast<ConsumerContext*>(ctx_ptr);
    for (int i = 0; i < ctx->count; ++i) {
        ctx->deque->pop_front(ctx->values[i], K_FOREVER);
    }
    k_sem_give(ctx->done_sem);
}

struct BlockingContext {
    MPMCDeque<int, SmallCapacity>* deque;
    int                            result;
    bool                           popped;
    k_sem*                         started_sem;
    k_sem*                         done_sem;
};

void blocking_pop_entry(void* ctx_ptr, void*, void*) {
    auto* ctx = static_cast<BlockingContext*>(ctx_ptr);
    k_sem_give(ctx->started_sem);
    ctx->popped = ctx->deque->pop_front(ctx->result, K_MSEC(200));
    k_sem_give(ctx->done_sem);
}

struct BlockingPushContext {
    MPMCDeque<int, SmallCapacity>* deque;
    int                            value;
    bool                           pushed;
    k_sem*                         started_sem;
    k_sem*                         done_sem;
};

void blocking_push_entry(void* ctx_ptr, void*, void*) {
    auto* ctx = static_cast<BlockingPushContext*>(ctx_ptr);
    k_sem_give(ctx->started_sem);
    ctx->pushed = ctx->deque->push_back(ctx->value, K_MSEC(200));
    k_sem_give(ctx->done_sem);
}

void producer_front_entry(void* ctx_ptr, void*, void*) {
    auto* ctx = static_cast<ProducerContext*>(ctx_ptr);
    for (int i = 0; i < ctx->count; ++i) {
        ctx->deque->push_front(ctx->base_value + i, K_FOREVER);
    }
    k_sem_give(ctx->done_sem);
}

void launch_producer_back(
    k_thread*                       thread,
    k_thread_stack_t*               stack,
    ProducerContext*                context,
    MPMCDeque<int, MediumCapacity>* deque,
    int                             base_value,
    int                             count,
    k_sem*                          done_sem
) {
    *context = {deque, base_value, count, done_sem};
    k_sem_init(done_sem, 0, 1);
    k_thread_create(
        thread,
        stack,
        ThreadStackSize,
        producer_back_entry,
        context,
        nullptr,
        nullptr,
        K_PRIO_PREEMPT(5),
        0,
        K_NO_WAIT
    );
}

void launch_producer_front(
    k_thread*                       thread,
    k_thread_stack_t*               stack,
    ProducerContext*                context,
    MPMCDeque<int, MediumCapacity>* deque,
    int                             base_value,
    int                             count,
    k_sem*                          done_sem
) {
    *context = {deque, base_value, count, done_sem};
    k_sem_init(done_sem, 0, 1);
    k_thread_create(
        thread,
        stack,
        ThreadStackSize,
        producer_front_entry,
        context,
        nullptr,
        nullptr,
        K_PRIO_PREEMPT(5),
        0,
        K_NO_WAIT
    );
}

void launch_consumer_front(
    k_thread*                       thread,
    k_thread_stack_t*               stack,
    ConsumerContext*                context,
    MPMCDeque<int, MediumCapacity>* deque,
    int*                            values,
    int                             count,
    k_sem*                          done_sem
) {
    *context = {deque, values, count, done_sem};
    k_sem_init(done_sem, 0, 1);
    k_thread_create(
        thread,
        stack,
        ThreadStackSize,
        consumer_front_entry,
        context,
        nullptr,
        nullptr,
        K_PRIO_PREEMPT(5),
        0,
        K_NO_WAIT
    );
}

void reset_destructor_count() { TestObject::destructor_count = 0; }
} // namespace

static void test_teardown(void*) {
    if (producer_thread1.base.thread_state != 0) {
        k_thread_join(&producer_thread1, K_MSEC(100));
        k_thread_abort(&producer_thread1);
    }
    if (producer_thread2.base.thread_state != 0) {
        k_thread_join(&producer_thread2, K_MSEC(100));
        k_thread_abort(&producer_thread2);
    }
    if (consumer_thread1.base.thread_state != 0) {
        k_thread_join(&consumer_thread1, K_MSEC(100));
        k_thread_abort(&consumer_thread1);
    }
    if (consumer_thread2.base.thread_state != 0) {
        k_thread_join(&consumer_thread2, K_MSEC(100));
        k_thread_abort(&consumer_thread2);
    }
}

ZTEST_SUITE(mpmc_deque_suite, NULL, NULL, NULL, test_teardown, NULL);

ZTEST(mpmc_deque_suite, test_basic_operations) {
    MPMCDeque<int, SmallCapacity> deque;

    zassert_true(deque.empty());
    zassert_false(deque.full());
    zassert_equal(0, deque.size());

    zassert_true(deque.push_back(1, K_NO_WAIT));
    zassert_equal(1, deque.size());
    zassert_false(deque.empty());

    zassert_true(deque.push_front(2, K_NO_WAIT));
    zassert_equal(2, deque.size());

    int value;
    zassert_true(deque.pop_front(value, K_NO_WAIT));
    zassert_equal(2, value);
    zassert_equal(1, deque.size());

    zassert_true(deque.pop_back(value, K_NO_WAIT));
    zassert_equal(1, value);
    zassert_true(deque.empty());
}

ZTEST(mpmc_deque_suite, test_boundary_conditions) {
    MPMCDeque<int, SmallCapacity> deque;

    for (size_t i = 0; i < SmallCapacity; ++i) {
        zassert_true(deque.push_back(static_cast<int>(i), K_NO_WAIT));
    }
    zassert_true(deque.full());

    zassert_false(deque.push_back(999, K_NO_WAIT));
    zassert_false(deque.push_front(999, K_NO_WAIT));

    for (size_t i = 0; i < SmallCapacity; ++i) {
        int value;
        zassert_true(deque.pop_front(value, K_NO_WAIT));
        zassert_equal(static_cast<int>(i), value);
    }
    zassert_true(deque.empty());

    int dummy;
    zassert_false(deque.pop_front(dummy, K_NO_WAIT));
    zassert_false(deque.pop_back(dummy, K_NO_WAIT));
}

ZTEST(mpmc_deque_suite, test_deque_specifics) {
    MPMCDeque<int, SmallCapacity> deque;
    int                           value;

    zassert_true(deque.push_back(1, K_NO_WAIT));
    zassert_true(deque.push_front(2, K_NO_WAIT));
    zassert_true(deque.push_back(3, K_NO_WAIT));
    zassert_true(deque.push_front(4, K_NO_WAIT));
    zassert_true(deque.pop_front(value, K_NO_WAIT));
    zassert_equal(4, value);
    zassert_true(deque.pop_back(value, K_NO_WAIT));
    zassert_equal(3, value);
    zassert_true(deque.pop_front(value, K_NO_WAIT));
    zassert_equal(2, value);
    zassert_true(deque.pop_back(value, K_NO_WAIT));
    zassert_equal(1, value);
    zassert_true(deque.empty());

    zassert_true(deque.push_back(10, K_NO_WAIT));
    zassert_true(deque.push_back(20, K_NO_WAIT));
    zassert_true(deque.push_back(30, K_NO_WAIT));
    zassert_true(deque.pop_back(value, K_NO_WAIT));
    zassert_equal(30, value);
    zassert_true(deque.pop_back(value, K_NO_WAIT));
    zassert_equal(20, value);
    zassert_true(deque.pop_back(value, K_NO_WAIT));
    zassert_equal(10, value);

    zassert_true(deque.push_back(10, K_NO_WAIT));
    zassert_true(deque.push_back(20, K_NO_WAIT));
    zassert_true(deque.push_back(30, K_NO_WAIT));
    zassert_true(deque.pop_front(value, K_NO_WAIT));
    zassert_equal(10, value);
    zassert_true(deque.pop_front(value, K_NO_WAIT));
    zassert_equal(20, value);
    zassert_true(deque.pop_front(value, K_NO_WAIT));
    zassert_equal(30, value);
}

ZTEST(mpmc_deque_suite, test_move_operations) {
    MPMCDeque<MoveOnlyObject, SmallCapacity> deque;
    MoveOnlyObject                           back_obj(42);
    MoveOnlyObject                           front_obj(24);

    zassert_true(deque.push_back(etl::move(back_obj), K_NO_WAIT));
    zassert_true(deque.push_front(etl::move(front_obj), K_NO_WAIT));
    zassert_equal(2, deque.size());

    MoveOnlyObject obj(0);
    zassert_true(deque.pop_front(obj, K_NO_WAIT));
    zassert_equal(24, obj.value);
    zassert_true(deque.pop_back(obj, K_NO_WAIT));
    zassert_equal(42, obj.value);

    MPMCDeque<ConstructionCounter, SmallCapacity> deque2;
    ConstructionCounter::reset();
    zassert_true(deque2.emplace_back(K_NO_WAIT, 42));
    zassert_equal(1, ConstructionCounter::construction_count);
    zassert_true(deque2.emplace_front(K_NO_WAIT, 24));
    zassert_equal(2, ConstructionCounter::construction_count);

    ConstructionCounter::reset();
    ConstructionCounter temp(99);
    zassert_equal(1, ConstructionCounter::construction_count);
    zassert_true(deque2.push_back(etl::move(temp), K_NO_WAIT));
    zassert_equal(2, ConstructionCounter::construction_count);

    ConstructionCounter result(0);
    zassert_true(deque2.pop_front(result, K_NO_WAIT));
    zassert_equal(24, result.value);
}

ZTEST(mpmc_deque_suite, test_destructor_safety) {
    reset_destructor_count();

    {
        MPMCDeque<TestObject, SmallCapacity> deque;

        for (int i = 0; i < static_cast<int>(SmallCapacity) - 1; ++i) {
            zassert_true(deque.push_back(TestObject(i), K_NO_WAIT));
        }

        TestObject obj;
        zassert_true(deque.pop_front(obj, K_NO_WAIT));

        zassert_equal(SmallCapacity, TestObject::destructor_count);
    }

    zassert_equal(SmallCapacity + SmallCapacity - 1, TestObject::destructor_count);
}

ZTEST(mpmc_deque_suite, test_concurrent_producers) {
    MPMCDeque<int, MediumCapacity> deque;
    k_sem                          done1, done2;
    ProducerContext                ctx1, ctx2;
    constexpr int                  ItemsPerProducer = 6;

    launch_producer_back(&producer_thread1, producer_stack1, &ctx1, &deque, 100, ItemsPerProducer, &done1);
    launch_producer_back(&producer_thread2, producer_stack2, &ctx2, &deque, 200, ItemsPerProducer, &done2);

    k_sem_take(&done1, K_FOREVER);
    k_sem_take(&done2, K_FOREVER);

    zassert_equal(ItemsPerProducer * 2, deque.size());

    int count_100s = 0;
    int count_200s = 0;
    for (int i = 0; i < ItemsPerProducer * 2; ++i) {
        int value;
        zassert_true(deque.pop_front(value, K_NO_WAIT));
        if (value >= 100 && value < 200) ++count_100s;
        if (value >= 200 && value < 300) ++count_200s;
    }
    zassert_equal(ItemsPerProducer, count_100s);
    zassert_equal(ItemsPerProducer, count_200s);
}

ZTEST(mpmc_deque_suite, test_concurrent_consumers) {
    MPMCDeque<int, MediumCapacity> deque;
    k_sem                          done1, done2;
    ConsumerContext                ctx1, ctx2;
    constexpr int                  TotalItems = 10;
    int                            values1[TotalItems / 2];
    int                            values2[TotalItems / 2];

    for (int i = 0; i < TotalItems; ++i) {
        zassert_true(deque.push_back(i, K_NO_WAIT));
    }

    launch_consumer_front(&producer_thread1, producer_stack1, &ctx1, &deque, values1, TotalItems / 2, &done1);
    launch_consumer_front(&producer_thread2, producer_stack2, &ctx2, &deque, values2, TotalItems / 2, &done2);

    k_sem_take(&done1, K_FOREVER);
    k_sem_take(&done2, K_FOREVER);

    zassert_true(deque.empty());

    bool seen[TotalItems] = {};
    for (int i = 0; i < TotalItems / 2; ++i) {
        zassert_false(seen[values1[i]]);
        seen[values1[i]] = true;
        zassert_false(seen[values2[i]]);
        seen[values2[i]] = true;
    }
}

ZTEST(mpmc_deque_suite, test_stress_bidirectional) {
    MPMCDeque<int, MediumCapacity> deque;
    k_sem                          done1, done2;
    ProducerContext                ctx1, ctx2;
    constexpr int                  ItemsPerThread = 4;

    launch_producer_back(&producer_thread1, producer_stack1, &ctx1, &deque, 300, ItemsPerThread, &done1);
    launch_producer_front(&producer_thread2, producer_stack2, &ctx2, &deque, 400, ItemsPerThread, &done2);

    k_sem_take(&done1, K_FOREVER);
    k_sem_take(&done2, K_FOREVER);

    zassert_equal(ItemsPerThread * 2, deque.size());

    int count_300s = 0;
    int count_400s = 0;
    for (int i = 0; i < ItemsPerThread * 2; ++i) {
        int value;
        zassert_true(deque.pop_front(value, K_NO_WAIT));
        if (value >= 300 && value < 400) ++count_300s;
        if (value >= 400 && value < 500) ++count_400s;
    }
    zassert_equal(ItemsPerThread, count_300s);
    zassert_equal(ItemsPerThread, count_400s);
    zassert_true(deque.empty());
}

ZTEST(mpmc_deque_suite, test_blocking_pop) {
    MPMCDeque<int, SmallCapacity> deque;
    k_sem                         started, done;

    k_sem_init(&started, 0, 1);
    k_sem_init(&done, 0, 1);

    BlockingContext ctx = {&deque, -1, false, &started, &done};

    k_thread_create(
        &producer_thread1,
        producer_stack1,
        ThreadStackSize,
        blocking_pop_entry,
        &ctx,
        nullptr,
        nullptr,
        K_PRIO_PREEMPT(5),
        0,
        K_NO_WAIT
    );

    k_sem_take(&started, K_FOREVER);
    k_sleep(K_MSEC(50));

    zassert_true(deque.push_back(42, K_NO_WAIT));

    k_sem_take(&done, K_FOREVER);

    zassert_true(ctx.popped);
    zassert_equal(42, ctx.result);
}

ZTEST(mpmc_deque_suite, test_blocking_push) {
    MPMCDeque<int, SmallCapacity> deque;
    k_sem                         started, done;

    for (size_t i = 0; i < SmallCapacity; ++i) {
        zassert_true(deque.push_back(static_cast<int>(i), K_NO_WAIT));
    }

    k_sem_init(&started, 0, 1);
    k_sem_init(&done, 0, 1);

    BlockingPushContext ctx = {&deque, 99, false, &started, &done};

    k_thread_create(
        &producer_thread1,
        producer_stack1,
        ThreadStackSize,
        blocking_push_entry,
        &ctx,
        nullptr,
        nullptr,
        K_PRIO_PREEMPT(5),
        0,
        K_NO_WAIT
    );

    k_sem_take(&started, K_FOREVER);
    k_sleep(K_MSEC(50));

    int value;
    zassert_true(deque.pop_front(value, K_NO_WAIT));

    k_sem_take(&done, K_FOREVER);

    zassert_true(ctx.pushed);
    zassert_equal(SmallCapacity, deque.size());
}

ZTEST(mpmc_deque_suite, test_concurrent_producer_consumer) {
    MPMCDeque<int, MediumCapacity> deque;
    k_sem                          prod_done1, prod_done2, cons_done1, cons_done2;
    ProducerContext                prod_ctx1, prod_ctx2;
    ConsumerContext                cons_ctx1, cons_ctx2;
    constexpr int                  ItemsPerProducer = 8;
    int                            values1[ItemsPerProducer];
    int                            values2[ItemsPerProducer];

    launch_producer_back(&producer_thread1, producer_stack1, &prod_ctx1, &deque, 0, ItemsPerProducer, &prod_done1);
    launch_producer_back(&producer_thread2, producer_stack2, &prod_ctx2, &deque, 100, ItemsPerProducer, &prod_done2);
    launch_consumer_front(&consumer_thread1, consumer_stack1, &cons_ctx1, &deque, values1, ItemsPerProducer, &cons_done1);
    launch_consumer_front(&consumer_thread2, consumer_stack2, &cons_ctx2, &deque, values2, ItemsPerProducer, &cons_done2);

    k_sem_take(&prod_done1, K_FOREVER);
    k_sem_take(&prod_done2, K_FOREVER);
    k_sem_take(&cons_done1, K_FOREVER);
    k_sem_take(&cons_done2, K_FOREVER);

    zassert_true(deque.empty());

    // verify all items from both producers were consumed (no loss, no duplication)
    int count_0s   = 0;
    int count_100s = 0;
    for (int i = 0; i < ItemsPerProducer; ++i) {
        if (values1[i] >= 0 && values1[i] < 100) ++count_0s;
        if (values1[i] >= 100 && values1[i] < 200) ++count_100s;
        if (values2[i] >= 0 && values2[i] < 100) ++count_0s;
        if (values2[i] >= 100 && values2[i] < 200) ++count_100s;
    }
    zassert_equal(ItemsPerProducer, count_0s);
    zassert_equal(ItemsPerProducer, count_100s);
}
