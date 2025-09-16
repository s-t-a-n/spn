#include <spn/threading/mutex.hpp>
#include <spn/threading/thread.hpp>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_mutex, LOG_LEVEL_INF);

ZTEST_SUITE(mutex_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(mutex_suite, test_mutex_basic_operations) {
    spn::Mutex mutex;

    zassert_equal(mutex.lock(), 0, "Mutex lock should succeed");
    zassert_equal(mutex.unlock(), 0, "Mutex unlock should succeed");
}

ZTEST(mutex_suite, test_mutex_lockguard_raii) {
    spn::Mutex mutex;

    {
        auto guard = mutex.lockguard();
    }

    zassert_equal(mutex.lock(), 0, "Mutex should be available after lockguard destruction");
    zassert_equal(mutex.unlock(), 0, "Mutex unlock should succeed");
}

ZTEST(mutex_suite, test_mutex_with_lock_success) {
    spn::Mutex mutex;
    auto       executed = false;

    auto result = mutex.with_lock([&executed]() {
        executed = true;
        return 42;
    });

    zassert_true(executed, "Lambda should have been executed");
    zassert_equal(result, 42, "Lambda return value should be preserved");
}

ZTEST(mutex_suite, test_mutex_with_lock_void) {
    spn::Mutex mutex;
    auto       counter = int{0};

    mutex.with_lock([&counter]() { counter = 100; });

    zassert_equal(counter, 100, "Lambda should have modified counter");
}

ZTEST(mutex_suite, test_mutex_timeout) {
    spn::Mutex mutex;

    // Test that reentrant locks work even with K_NO_WAIT
    zassert_equal(mutex.lock(K_NO_WAIT), 0, "First lock should succeed immediately");
    zassert_equal(mutex.lock(K_NO_WAIT), 0, "Second lock should also succeed (reentrant)");
    zassert_equal(mutex.lock(K_NO_WAIT), 0, "Third lock should also succeed (reentrant)");

    // Must unlock same number of times as locked
    zassert_equal(mutex.unlock(), 0, "First unlock should succeed");
    zassert_equal(mutex.unlock(), 0, "Second unlock should succeed");
    zassert_equal(mutex.unlock(), 0, "Third unlock should succeed");
}

ZTEST(mutex_suite, test_mutex_reentrant) {
    spn::Mutex mutex;

    zassert_equal(mutex.lock(), 0, "First lock should succeed");
    zassert_equal(mutex.lock(), 0, "Reentrant lock should succeed");
    zassert_equal(mutex.unlock(), 0, "First unlock should succeed");
    zassert_equal(mutex.unlock(), 0, "Second unlock should succeed");
}

struct ThreadTestData {
    spn::Mutex*   mutex;
    int*          shared_counter;
    int           thread_id;
    int           iterations;
    volatile bool start_flag;
    volatile bool work_completed;
};

static void mutex_contention_thread(ThreadTestData* data, spn::ThreadState state) {
    if (state != spn::ThreadState::RUNNING || data->work_completed || !data->start_flag) {
        k_msleep(1);
        return;
    }

    for (auto i = int{0}; i < data->iterations; ++i) {
        data->mutex->with_lock([data]() {
            auto current = *data->shared_counter;
            k_busy_wait(1);
            *data->shared_counter = current + 1;
        });
    }

    data->work_completed = true;
}

ZTEST(mutex_suite, test_mutex_contention) {
    spn::Mutex mutex;
    auto       shared_counter = int{0};
    const auto iterations     = 50;
    const auto num_threads    = 3;

    auto data1 = ThreadTestData{&mutex, &shared_counter, 1, iterations, false, false};
    auto data2 = ThreadTestData{&mutex, &shared_counter, 2, iterations, false, false};
    auto data3 = ThreadTestData{&mutex, &shared_counter, 3, iterations, false, false};

    using TestThread = spn::Thread<1024, ThreadTestData>;
    auto delegate    = TestThread::Delegate::create<mutex_contention_thread>();

    auto thread1 = TestThread{delegate, &data1, 5, "thread1"};
    auto thread2 = TestThread{delegate, &data2, 5, "thread2"};
    auto thread3 = TestThread{delegate, &data3, 5, "thread3"};

    zassert_equal(thread1.start(), 0, "Thread 1 should start successfully");
    zassert_equal(thread2.start(), 0, "Thread 2 should start successfully");
    zassert_equal(thread3.start(), 0, "Thread 3 should start successfully");

    k_msleep(10);

    data1.start_flag = true;
    data2.start_flag = true;
    data3.start_flag = true;

    // Wait for all threads to complete their work
    auto timeout_count = int{0};
    while ((!data1.work_completed || !data2.work_completed || !data3.work_completed) && timeout_count < 5000) {
        k_msleep(1);
        timeout_count++;
    }

    zassert_true(data1.work_completed, "Thread 1 should complete its work");
    zassert_true(data2.work_completed, "Thread 2 should complete its work");
    zassert_true(data3.work_completed, "Thread 3 should complete its work");

    zassert_equal(shared_counter, iterations * num_threads, "Shared counter should equal total iterations");
}