#include "spn/system/shutdown.hpp"

#include <etl/initializer_list.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

using namespace spn::system;

extern "C" void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf* esf);

class MockShutdownManager : public ShutdownManager {
public:
    shutdown_reason last_reason    = shutdown_reason::unknown;
    int             shutdown_count = 0;
    unsigned int    fatal_reason   = 0;

    void request_shutdown(shutdown_reason reason) override {
        last_reason = reason;
        shutdown_count++;
    }

    void reset() {
        last_reason    = shutdown_reason::unknown;
        shutdown_count = 0;
        fatal_reason   = 0;
    }
};

static void fatal_hook_test_before(void* fixture) { set_shutdown_manager(nullptr); }

ZTEST_SUITE(fatal_hook_suite, NULL, NULL, fatal_hook_test_before, NULL, NULL);

ZTEST(fatal_hook_suite, test_fatal_reason_mapping) {
    MockShutdownManager mgr;
    set_shutdown_manager(&mgr);

    request_shutdown(shutdown_reason::fatal_oops);
    k_sleep(K_MSEC(10));

    zassert_equal(1, mgr.shutdown_count);
    zassert_equal(shutdown_reason::fatal_oops, mgr.last_reason);
}

ZTEST(fatal_hook_suite, test_shutdown_reason_values) {
    zassert_true(static_cast<int>(shutdown_reason::unknown) == 0);
    zassert_true(static_cast<int>(shutdown_reason::user_request) != 0);
    zassert_true(static_cast<int>(shutdown_reason::fatal_oops) != 0);
    zassert_true(static_cast<int>(shutdown_reason::exception_thrown) != 0);

    zassert_true(static_cast<int>(shutdown_reason::fatal_oops) != static_cast<int>(shutdown_reason::exception_thrown));
}

ZTEST(fatal_hook_suite, test_all_fatal_reasons) {
    MockShutdownManager mgr;
    set_shutdown_manager(&mgr);

    auto fatal_reasons = {shutdown_reason::fatal_assert, shutdown_reason::fatal_oops};

    for (auto reason : fatal_reasons) {
        mgr.reset();
        request_shutdown(reason);
        k_sleep(K_MSEC(10));

        zassert_equal(1, mgr.shutdown_count);
        zassert_equal(reason, mgr.last_reason);
    }
}