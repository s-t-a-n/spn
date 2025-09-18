#include "spn/threading/mutex.hpp"
#include "spn/threading/thread.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spn_mutex_sample, LOG_LEVEL_INF);

static spn::Mutex global_mutex;
static int        shared_counter = 0;

struct WorkerData {
    int         worker_id;
    int         iterations;
    const char* name;
};

static void worker_thread(WorkerData* data, spn::ThreadState state) {
    if (state == spn::ThreadState::RUNNING) {
        for (int i = 0; i < data->iterations; ++i) {
            global_mutex.with_lock([data, i]() {
                int current = shared_counter;
                LOG_INF("Worker %d (iteration %d): counter was %d", data->worker_id, i + 1, current);
                k_busy_wait(100);
                shared_counter = current + 1;
                LOG_INF("Worker %d (iteration %d): counter now %d", data->worker_id, i + 1, shared_counter);
            });
            k_msleep(50);
        }
        LOG_INF("Worker %d completed all iterations", data->worker_id);
    }
}

static void demonstrate_basic_usage() {
    LOG_INF("=== Basic Mutex Usage ===");

    spn::Mutex local_mutex;

    LOG_INF("Manual lock/unlock:");
    if (local_mutex.lock() == 0) {
        LOG_INF("Mutex locked successfully");
        k_msleep(10);
        local_mutex.unlock();
        LOG_INF("Mutex unlocked");
    }

    LOG_INF("RAII with lockguard:");
    {
        auto guard = local_mutex.lockguard();
        LOG_INF("Mutex locked via lockguard");
        k_msleep(10);
    }
    LOG_INF("Mutex automatically unlocked");

    LOG_INF("Lambda execution with with_lock:");
    int result = local_mutex.with_lock([]() {
        LOG_INF("Executing in locked context");
        k_msleep(10);
        return 42;
    });
    LOG_INF("Lambda returned: %d", result);
}

static void demonstrate_timeout_handling() {
    LOG_INF("\n=== Timeout Handling ===");

    spn::Mutex timeout_mutex;

    timeout_mutex.lock();
    LOG_INF("Mutex locked, attempting timeout lock...");

    if (timeout_mutex.lock(K_NO_WAIT) != 0) {
        LOG_INF("Timeout lock failed as expected (no wait)");
    }

    if (timeout_mutex.lock(K_MSEC(100)) != 0) {
        LOG_INF("Timeout lock failed as expected (100ms timeout)");
    }

    timeout_mutex.unlock();
    LOG_INF("Mutex unlocked, timeout test complete");
}

static void demonstrate_reentrant_locking() {
    LOG_INF("\n=== Reentrant Locking ===");

    spn::Mutex reentrant_mutex;

    LOG_INF("First lock...");
    reentrant_mutex.lock();

    LOG_INF("Nested lock (reentrant)...");
    reentrant_mutex.lock();

    LOG_INF("Both locks acquired successfully");

    LOG_INF("First unlock...");
    reentrant_mutex.unlock();

    LOG_INF("Second unlock...");
    reentrant_mutex.unlock();

    LOG_INF("All locks released");
}

static void demonstrate_multi_threaded_access() {
    LOG_INF("\n=== Multi-threaded Shared Resource Protection ===");

    shared_counter = 0;

    WorkerData worker1_data = {1, 3, "Producer"};
    WorkerData worker2_data = {2, 3, "Consumer"};

    auto delegate1 = spn::Thread<2048, WorkerData>::Delegate::create<worker_thread>();
    auto delegate2 = spn::Thread<2048, WorkerData>::Delegate::create<worker_thread>();

    spn::Thread<2048, WorkerData> worker1(delegate1, &worker1_data, 5, "worker1");
    spn::Thread<2048, WorkerData> worker2(delegate2, &worker2_data, 5, "worker2");

    LOG_INF("Starting worker threads...");

    if (worker1.start() != 0) {
        LOG_ERR("Failed to start worker1");
        return;
    }

    if (worker2.start() != 0) {
        LOG_ERR("Failed to start worker2");
        return;
    }

    k_msleep(2000);

    LOG_INF("Stopping worker threads...");
    worker1.stop();
    worker2.stop();

    LOG_INF("Final shared counter value: %d", shared_counter);
}

int main(void) {
    LOG_INF("SPN Mutex Sample Application");

    demonstrate_basic_usage();
    demonstrate_timeout_handling();
    demonstrate_reentrant_locking();
    demonstrate_multi_threaded_access();

    LOG_INF("\nMutex sample completed successfully");
    return 0;
}