#include "spn/threading/refguard.hpp"
#include "spn/threading/thread.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spn_refguard_sample, LOG_LEVEL_INF);

static spn::RefGuard shared_guard;

struct SharedWorkerData {
    int worker_id;
    int work_count;
};

static void shared_worker_entry(SharedWorkerData* data, spn::ThreadState state) {
    if (state != spn::ThreadState::RUNNING) return;

    if (data->work_count < 3) {
        auto ref = shared_guard.try_acquire_scoped();
        if (ref) {
            data->work_count++;
            LOG_INF("Worker %d: acquired shared ref (refcount=%u)", data->worker_id, shared_guard.ref_count());
            k_msleep(50);
        }
        k_msleep(10);
    } else {
        k_msleep(10);
    }
    LOG_INF("Worker %d: released shared ref", data->worker_id);
}

static void shared_access() {
    LOG_INF("=== Shared Concurrent Access ===");

    SharedWorkerData worker1_data = {1, 0};
    SharedWorkerData worker2_data = {2, 0};

    auto delegate1 = spn::Thread<2048, SharedWorkerData>::Delegate::create<shared_worker_entry>();
    auto delegate2 = spn::Thread<2048, SharedWorkerData>::Delegate::create<shared_worker_entry>();

    spn::Thread<2048, SharedWorkerData> worker1(delegate1, &worker1_data, 5, "worker1");
    spn::Thread<2048, SharedWorkerData> worker2(delegate2, &worker2_data, 5, "worker2");

    LOG_INF("Starting workers - they will hold shared refs concurrently");
    if (worker1.start() != 0 || worker2.start() != 0) {
        LOG_ERR("Failed to start workers");
        return;
    }

    k_msleep(400);

    worker1.stop();
    worker2.stop();

    LOG_INF("Final refcount: %u", shared_guard.ref_count());
}

static void exclusive_reconfiguration() {
    LOG_INF("\n=== Exclusive Access for Reconfiguration ===");

    spn::RefGuard guard;

    {
        LOG_INF("Acquiring shared reference...");
        auto shared_ref = guard.try_acquire_scoped();
        if (shared_ref) {
            LOG_INF("Shared ref acquired (refcount=%u)", guard.ref_count());
        }

        LOG_INF("Attempting exclusive while shared ref exists...");
        if (!guard.try_acquire_exclusive()) {
            LOG_INF("Exclusive acquisition failed (shared refs exist)");
        }
    }
    LOG_INF("Shared ref released (refcount=%u)", guard.ref_count());

    LOG_INF("Acquiring exclusive lock...");
    if (guard.try_acquire_exclusive()) {
        LOG_INF("Exclusive acquired (refcount=%u)", guard.ref_count());

        LOG_INF("Attempting shared acquisition while exclusive held...");
        auto blocked_ref = guard.try_acquire_scoped();
        if (!blocked_ref) {
            LOG_INF("Shared acquisition correctly blocked");
        }

        k_msleep(50);

        LOG_INF("Releasing exclusive...");
        guard.release_exclusive();
        LOG_INF("Exclusive released (refcount=%u)", guard.ref_count());

        LOG_INF("Shared acquisition now succeeds...");
        auto new_ref = guard.try_acquire_scoped();
        if (new_ref) {
            LOG_INF("Shared ref acquired after exclusive release");
        }
    }
}

struct ShutdownWorkerData {
    int            worker_id;
    spn::RefGuard* guard;
};

static void shutdown_worker_entry(ShutdownWorkerData* data, spn::ThreadState state) {
    if (state != spn::ThreadState::RUNNING) return;

    auto ref = data->guard->try_acquire_scoped();
    if (ref) {
        LOG_INF("Worker %d: using resource (refcount=%u)", data->worker_id, data->guard->ref_count());
        k_msleep(50);
    } else {
        k_msleep(10);
    }
}

static void graceful_teardown() {
    LOG_INF("\n=== Graceful Teardown ===");

    spn::RefGuard teardown_guard;

    ShutdownWorkerData worker_data = {1, &teardown_guard};

    auto delegate = spn::Thread<2048, ShutdownWorkerData>::Delegate::create<shutdown_worker_entry>();
    spn::Thread<2048, ShutdownWorkerData> worker(delegate, &worker_data, 5, "worker");

    LOG_INF("Starting worker using resource");
    if (worker.start() != 0) {
        LOG_ERR("Failed to start worker");
        return;
    }

    LOG_INF("Worker active, initiating shutdown...");
    k_msleep(100);

    LOG_INF("Denying new acquisitions...");
    teardown_guard.deny_acquisitions();

    auto ref = teardown_guard.try_acquire_scoped();
    if (!ref) {
        LOG_INF("New acquisition correctly denied");
    }

    LOG_INF("Waiting for active references to release...");
    auto rc = teardown_guard.wait_for_release(K_MSEC(500));
    if (rc == 0) {
        LOG_INF("All references released");
    } else {
        LOG_ERR("Timeout: %i", rc);
    }

    LOG_INF("Performing final teardown...");
    rc = teardown_guard.teardown(K_MSEC(500));
    if (rc == 0) {
        LOG_INF("Teardown complete (refcount=%u)", teardown_guard.ref_count());
    } else {
        LOG_ERR("Teardown failed: %i", rc);
    }

    worker.stop();
}

int main(void) {
    LOG_INF("SPN RefGuard Sample Application\n");

    shared_access();
    exclusive_reconfiguration();
    graceful_teardown();

    LOG_INF("\nRefGuard sample completed successfully");
    if constexpr (IS_ENABLED(CONFIG_BOARD_NATIVE_SIM)) exit(0);
    return 0;
}
