#include "spn/debugging/assert.hpp"
#include "spn/system/exception.hpp"
#include "spn/system/shutdown.hpp"

#include <etl/vector.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

using namespace spn::system;

class MockShutdownManager final : public ShutdownManager {
public:
    struct ShutdownCall {
        shutdown_reason reason;
        uint32_t        timestamp;
    };

    etl::vector<ShutdownCall, 10> calls;
    bool                          shutdown_requested = false;

    void request_shutdown(shutdown_reason reason) override {
        calls.push_back({reason, k_uptime_get_32()});

        shutdown_requested = true;
    }

    void reset() {
        calls.clear();
        shutdown_requested = false;
    }

    bool was_called_with(shutdown_reason reason) const {
        for (const auto& call : calls) {
            if (call.reason == reason) return true;
        }
        return false;
    }
};

class MockExceptionHandler final : public ExceptionHandler {
public:
    struct ExceptionCall {
        const char* exception_type;
        const char* exception_what;
        uint32_t    timestamp;
    };

    etl::vector<ExceptionCall, 10> calls;

    void handle_exception(const exception& ex) override {
        calls.push_back({ex.error_type(), ex.what(), k_uptime_get_32()});
    }

    void reset() { calls.clear(); }

    bool was_called_with_type(const char* type) const {
        for (const auto& call : calls) {
            if (strcmp(call.exception_type, type) == 0) return true;
        }
        return false;
    }
};

static void integration_test_before(void* fixture) {
    set_shutdown_manager(nullptr);
    set_exception_handler(nullptr);
}

ZTEST_SUITE(integration_suite, NULL, NULL, integration_test_before, NULL, NULL);

ZTEST(integration_suite, test_exception_handler_to_shutdown_flow) {
    MockShutdownManager  shutdown_mgr;
    MockExceptionHandler exception_handler;

    set_shutdown_manager(&shutdown_mgr);
    set_exception_handler(&exception_handler);

    spn::runtime_exception ex("integration test exception");

    exception_handler.handle_exception(ex);
    request_shutdown(shutdown_reason::exception_thrown);

    k_sleep(K_MSEC(10));

    zassert_true(exception_handler.was_called_with_type("runtime exception"));
    zassert_true(shutdown_mgr.was_called_with(shutdown_reason::exception_thrown));
    zassert_true(shutdown_mgr.shutdown_requested);
}

ZTEST(integration_suite, test_assert_exception_integration_flow) {
    MockShutdownManager  shutdown_mgr;
    MockExceptionHandler exception_handler;

    set_shutdown_manager(&shutdown_mgr);
    set_exception_handler(&exception_handler);

    enable_assert_exceptions();

    spn::assertion_exception assert_ex("assert failure test");
    exception_handler.handle_exception(assert_ex);
    request_shutdown(shutdown_reason::exception_thrown);

    k_sleep(K_MSEC(10));

    zassert_true(exception_handler.was_called_with_type("assertion exception"));
    zassert_true(shutdown_mgr.was_called_with(shutdown_reason::exception_thrown));
}

ZTEST(integration_suite, test_multiple_exception_types_flow) {
    MockShutdownManager  shutdown_mgr;
    MockExceptionHandler exception_handler;

    set_shutdown_manager(&shutdown_mgr);
    set_exception_handler(&exception_handler);

    spn::runtime_exception   runtime_ex("runtime error");
    spn::logic_exception     logic_ex("logic error");
    spn::assertion_exception assert_ex("assertion error");

    exception_handler.handle_exception(runtime_ex);
    exception_handler.handle_exception(logic_ex);
    exception_handler.handle_exception(assert_ex);

    zassert_true(exception_handler.was_called_with_type("runtime exception"));
    zassert_true(exception_handler.was_called_with_type("logic exception"));
    zassert_true(exception_handler.was_called_with_type("assertion exception"));
    zassert_equal(3, exception_handler.calls.size());
}

ZTEST(integration_suite, test_handler_chain_integrity) {
    MockShutdownManager  shutdown_mgr1, shutdown_mgr2, shutdown_mgr3;
    MockExceptionHandler handler1, handler2, handler3;

    auto saved_shutdown1 = set_shutdown_manager(&shutdown_mgr1);
    auto saved_handler1  = set_exception_handler(&handler1);
    zassert_is_null(saved_shutdown1);
    zassert_is_null(saved_handler1);

    auto saved_shutdown2 = set_shutdown_manager(&shutdown_mgr2);
    auto saved_handler2  = set_exception_handler(&handler2);
    zassert_equal(&shutdown_mgr1, saved_shutdown2);
    zassert_equal(&handler1, saved_handler2);

    auto saved_shutdown3 = set_shutdown_manager(&shutdown_mgr3);
    auto saved_handler3  = set_exception_handler(&handler3);
    zassert_equal(&shutdown_mgr2, saved_shutdown3);
    zassert_equal(&handler2, saved_handler3);

    request_shutdown(shutdown_reason::user_request);
    k_sleep(K_MSEC(10));

    zassert_true(shutdown_mgr3.was_called_with(shutdown_reason::user_request));
    zassert_false(shutdown_mgr1.was_called_with(shutdown_reason::user_request));
    zassert_false(shutdown_mgr2.was_called_with(shutdown_reason::user_request));
}

ZTEST(integration_suite, test_concurrent_shutdown_requests) {
    MockShutdownManager shutdown_mgr;

    set_shutdown_manager(&shutdown_mgr);

    request_shutdown(shutdown_reason::user_request);
    request_shutdown(shutdown_reason::config_error);
    request_shutdown(shutdown_reason::network_failure);

    k_sleep(K_MSEC(50));

    zassert_equal(1, shutdown_mgr.calls.size());
    zassert_true(shutdown_mgr.was_called_with(shutdown_reason::user_request));

    shutdown_mgr.reset();

    request_shutdown(shutdown_reason::network_failure);
    k_sleep(K_MSEC(10));

    zassert_equal(1, shutdown_mgr.calls.size());
    zassert_true(shutdown_mgr.was_called_with(shutdown_reason::network_failure));
}

ZTEST(integration_suite, test_null_handler_graceful_degradation) {
    MockShutdownManager shutdown_mgr;

    set_shutdown_manager(&shutdown_mgr);

    zassert_is_null(exception_handler());

    request_shutdown(shutdown_reason::exception_thrown);
    k_sleep(K_MSEC(10));

    zassert_true(shutdown_mgr.was_called_with(shutdown_reason::exception_thrown));
}

ZTEST(integration_suite, test_shutdown_manager_cleanup_opportunity) {
    MockShutdownManager shutdown_mgr;

    set_shutdown_manager(&shutdown_mgr);

    request_shutdown(shutdown_reason::fatal_oops);
    k_sleep(K_MSEC(10));

    zassert_true(shutdown_mgr.was_called_with(shutdown_reason::fatal_oops));
    zassert_true(shutdown_mgr.shutdown_requested);
}
