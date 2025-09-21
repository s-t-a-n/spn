#include "spn/logging/logging.hpp"
#include "spn/system/exception.hpp"
#include "spn/system/shutdown.hpp"

#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(shutdown_sample);

class SampleShutdownManager : public spn::system::ShutdownManager {
public:
    void request_shutdown(spn::system::shutdown_reason reason) override {
        LOG_WRN("shutdown requested: reason %i", static_cast<int>(reason));

        switch (reason) {
        case spn::system::shutdown_reason::user_request: LOG_INF("performing graceful user shutdown"); break;
        case spn::system::shutdown_reason::exception_thrown: LOG_ERR("shutdown due to exception"); break;
        case spn::system::shutdown_reason::fatal_oops: LOG_ERR("shutdown due to fatal error"); break;
        default: LOG_WRN("shutdown for unknown reason"); break;
        }

        LOG_INF("[simulated cleanup tasks]");
        k_sleep(K_MSEC(100));

        LOG_INF("finalizing shutdown");
        spn::system::finalize_shutdown();
    }
};

int main(void) {
    LOG_INF("=== SPN System Shutdown Manager Sample ===");

    auto shutdown_mgr = SampleShutdownManager{};
    spn::system::set_shutdown_manager(&shutdown_mgr);

    LOG_INF("testing user-requested shutdown");
    spn::system::request_shutdown(spn::system::shutdown_reason::user_request);

    k_sleep(K_MSEC(500));

    LOG_INF("testing exception-triggered shutdown");
    spn::runtime_exception ex("sample exception causing shutdown");
    spn::throw_exception(ex);

    LOG_ERR("should not reach here - shutdown should have occurred");

    return 0;
}