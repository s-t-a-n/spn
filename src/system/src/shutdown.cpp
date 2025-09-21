#include "spn/system/shutdown.hpp"

#include "spn/logging/logging.hpp"
#include "spn/system/exception.hpp"

#include <zephyr/init.h>
#include <zephyr/kernel.h>

#ifdef CONFIG_BOARD_NATIVE_SIM
#    include <stdlib.h>
#else
#    include <zephyr/sys/reboot.h>
#endif

namespace spn::system {

LOG_MODULE_DECLARE(spn_system);

static ShutdownManager*  _shutdown_manager  = nullptr;
static ExceptionHandler* _exception_handler = nullptr;

struct ShutdownWork {
    k_work_delayable work;
    shutdown_reason  reason;
};

static void shutdown_work_handler(k_work* work) {
    auto shutdown_work = CONTAINER_OF(work, ShutdownWork, work);
    auto reason        = shutdown_work->reason;

    if (_shutdown_manager) {
        _shutdown_manager->request_shutdown(reason);
    } else {
        MLOG_WRN(spn_system, "no shutdown manager set, finalizing immediately");
        finalize_shutdown();
    }
}

static ShutdownWork _shutdown_work = {.work = {}, .reason = shutdown_reason::unknown};

static int init_shutdown_work() {
    k_work_init_delayable(&_shutdown_work.work, shutdown_work_handler);
    return 0;
}

SYS_INIT(init_shutdown_work, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

ShutdownManager* set_shutdown_manager(ShutdownManager* mgr) {
    auto old          = _shutdown_manager;
    _shutdown_manager = mgr;
    MLOG_INF(spn_system, "shutdown manager set: %p", mgr);
    return old;
}

ShutdownManager* shutdown_manager() { return _shutdown_manager; }

void request_shutdown(shutdown_reason reason) {
    MLOG_WRN(spn_system, "shutdown requested: %i", static_cast<int>(reason));
    _shutdown_work.reason = reason;
    k_work_schedule(&_shutdown_work.work, K_NO_WAIT);
}

ExceptionHandler* set_exception_handler(ExceptionHandler* handler) {
    auto old           = _exception_handler;
    _exception_handler = handler;
    MLOG_INF(spn_system, "exception handler set: %p", handler);
    return old;
}

ExceptionHandler* exception_handler() { return _exception_handler; }

void finalize_shutdown() {
#ifdef CONFIG_BOARD_NATIVE_SIM
    exit(0);
#else
    sys_reboot(SYS_REBOOT_WARM);
#endif
}

} // namespace spn::system