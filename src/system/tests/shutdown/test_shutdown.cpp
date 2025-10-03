#include "spn/system/shutdown.hpp"

#include <etl/initializer_list.h>
#include <zephyr/ztest.h>

using namespace spn::system;

class MockShutdownManager final : public ShutdownManager {
public:
    shutdown_reason last_reason    = shutdown_reason::unknown;
    int             shutdown_count = 0;

    void request_shutdown(shutdown_reason reason) override {
        last_reason = reason;
        shutdown_count++;
    }

    void reset() {
        last_reason    = shutdown_reason::unknown;
        shutdown_count = 0;
    }
};

static void shutdown_test_before(void* fixture) { set_shutdown_manager(nullptr); }

ZTEST_SUITE(shutdown_suite, NULL, NULL, shutdown_test_before, NULL, NULL);

ZTEST(shutdown_suite, test_shutdown_manager_basic) {
    MockShutdownManager mgr;

    set_shutdown_manager(&mgr);

    zassert_not_null(shutdown_manager());
    zassert_equal(&mgr, shutdown_manager());
    zassert_equal(0, mgr.shutdown_count);
}

ZTEST(shutdown_suite, test_shutdown_all_reasons) {
    MockShutdownManager mgr;
    set_shutdown_manager(&mgr);

    auto all_reasons = {
        shutdown_reason::unknown,
        shutdown_reason::user_request,
        shutdown_reason::config_error,
        shutdown_reason::network_failure,
        shutdown_reason::fatal_assert,
        shutdown_reason::fatal_oops,
        shutdown_reason::exception_thrown
    };

    for (auto reason : all_reasons) {
        mgr.reset();
        request_shutdown(reason);
        k_sleep(K_MSEC(10));

        zassert_equal(1, mgr.shutdown_count);
        zassert_equal(reason, mgr.last_reason);
    }
}

ZTEST(shutdown_suite, test_shutdown_manager_swapping) {
    MockShutdownManager mgr1;
    MockShutdownManager mgr2;

    auto original = set_shutdown_manager(&mgr1);
    zassert_is_null(original);
    zassert_equal(&mgr1, shutdown_manager());

    auto saved = set_shutdown_manager(&mgr2);
    zassert_equal(&mgr1, saved);
    zassert_equal(&mgr2, shutdown_manager());
}

ZTEST(shutdown_suite, test_concurrent_shutdown_requests) {
    MockShutdownManager mgr;
    set_shutdown_manager(&mgr);

    request_shutdown(shutdown_reason::user_request);
    request_shutdown(shutdown_reason::config_error);
    request_shutdown(shutdown_reason::network_failure);

    k_sleep(K_MSEC(50));

    zassert_equal(1, mgr.shutdown_count);
    zassert_equal(shutdown_reason::user_request, mgr.last_reason);

    // ensure subsequent requests are processed after the first completes
    mgr.reset();

    request_shutdown(shutdown_reason::network_failure);
    k_sleep(K_MSEC(10));

    zassert_equal(1, mgr.shutdown_count);
    zassert_equal(shutdown_reason::network_failure, mgr.last_reason);
}

ZTEST(shutdown_suite, test_null_shutdown_manager) {
    MockShutdownManager mgr;

    set_shutdown_manager(&mgr);
    auto saved_mgr = set_shutdown_manager(nullptr);
    zassert_equal(&mgr, saved_mgr);
    zassert_is_null(shutdown_manager());

    zassert_equal(0, mgr.shutdown_count);
}
