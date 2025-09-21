#include "spn/debugging/assert.hpp"
#include "spn/logging/logging.hpp"
#include "spn/system/exception.hpp"

#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(without_cpp_exceptions_sample);

class SampleShutdownManager : public spn::system::ShutdownManager {
public:
    void request_shutdown(spn::system::shutdown_reason reason) override {
        LOG_INF("shutdown requested: reason %i", static_cast<int>(reason));

        switch (reason) {
        case spn::system::shutdown_reason::exception_thrown:
            LOG_INF("shutdown due to exception (C++ exceptions disabled)");
            break;
        case spn::system::shutdown_reason::fatal_assert: LOG_INF("shutdown due to failed assertion"); break;
        default: LOG_INF("shutdown for other reason"); break;
        }

        LOG_INF("=== Sample Complete (shutting down) ===");
        spn::system::finalize_shutdown();
    }
};

int main(void) {
    LOG_INF("=== SPN System Sample (without C++ exceptions) ===");

    auto shutdown_mgr = SampleShutdownManager{};
    spn::system::set_shutdown_manager(&shutdown_mgr);

    LOG_INF("enabling exception-based assertions");
    spn::system::enable_assert_exceptions();

    LOG_INF("testing successful assertion");
    spn_assert(true);

    LOG_INF("creating runtime exception (will trigger shutdown)");
    spn::runtime_exception ex("sample runtime error");
    spn::throw_exception(ex);

    LOG_ERR("should not reach here - shutdown should have occurred");
    return 0;
}