#include "spn/debugging/assert.hpp"
#include "spn/system/exception.hpp"

#include <zephyr/ztest.h>

using namespace spn::system;

struct MockExceptionHandler : ExceptionHandler {
    const exception* last_exception = nullptr;
    int              handle_count   = 0;

    void handle_exception(const exception& ex) override {
        last_exception = &ex;
        handle_count++;
    }

    void reset() {
        last_exception = nullptr;
        handle_count   = 0;
    }
};

struct MockShutdownManager : ShutdownManager {
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

static void exception_test_before(void* fixture) {
    set_exception_handler(nullptr);
    set_shutdown_manager(nullptr);
    reset_shutdown_state();
}

ZTEST_SUITE(exception_suite, NULL, NULL, exception_test_before, NULL, NULL);

ZTEST(exception_suite, test_exception_handler_basic) {
    MockExceptionHandler handler;

    set_exception_handler(&handler);

    zassert_not_null(exception_handler());
    zassert_equal(&handler, exception_handler());
    zassert_equal(0, handler.handle_count);
}

ZTEST(exception_suite, test_exception_types) {
    exception           base_ex("base exception");
    runtime_exception   runtime_ex("runtime error");
    logic_exception     logic_ex("logic error");
    assertion_exception assert_ex("assertion failed");

    zassert_str_equal("exception", base_ex.error_type());
    zassert_str_equal("runtime exception", runtime_ex.error_type());
    zassert_str_equal("logic exception", logic_ex.error_type());
    zassert_str_equal("assertion exception", assert_ex.error_type());

    zassert_str_equal("base exception", base_ex.what());
    zassert_str_equal("runtime error", runtime_ex.what());
    zassert_str_equal("logic error", logic_ex.what());
    zassert_str_equal("assertion failed", assert_ex.what());
}

ZTEST(exception_suite, test_exception_assert_integration) {
    MockExceptionHandler handler;
    MockShutdownManager  shutdown_mgr;

    set_exception_handler(&handler);
    set_shutdown_manager(&shutdown_mgr);

    spn::system::enable_assert_exceptions();

    zassert_equal(0, handler.handle_count);
    zassert_equal(0, shutdown_mgr.shutdown_count);
}

ZTEST(exception_suite, test_exception_handler_swapping) {
    MockExceptionHandler handler1;
    MockExceptionHandler handler2;

    auto original = set_exception_handler(&handler1);
    zassert_is_null(original);
    zassert_equal(&handler1, exception_handler());

    auto saved = set_exception_handler(&handler2);
    zassert_equal(&handler1, saved);
    zassert_equal(&handler2, exception_handler());
}

ZTEST(exception_suite, test_null_exception_handler) {
    MockExceptionHandler handler;
    MockShutdownManager  shutdown_mgr;

    set_exception_handler(&handler);
    auto saved_handler = set_exception_handler(nullptr);
    zassert_equal(&handler, saved_handler);
    zassert_is_null(exception_handler());

    zassert_equal(0, handler.handle_count);
    zassert_equal(0, shutdown_mgr.shutdown_count);
}
