#include <spn/debugging/assert.hpp>
#include <zephyr/ztest.h>

static bool        handler_called = false;
static const char* last_condition = nullptr;
static const char* last_file      = nullptr;
static int         last_line      = 0;

static void test_assert_handler(const char* file, int line, const char* condition, const char* message) {
    handler_called = true;
    last_condition = condition;
    last_file      = file;
    last_line      = line;
}

static void test_expect_handler(const char* file, int line, const char* condition, const char* message) {
    handler_called = true;
    last_condition = condition;
    last_file      = file;
    last_line      = line;
}

static void reset_test_state(void* fixture) {
    handler_called = false;
    last_condition = nullptr;
    last_file      = nullptr;
    last_line      = 0;
}

ZTEST_SUITE(spn_debugging, NULL, NULL, reset_test_state, NULL, NULL);

ZTEST(spn_debugging, test_spn_assert_success) {
    spn_assert(true);
    zassert_false(handler_called, "Handler should not be called for successful assertion");
}

ZTEST(spn_debugging, test_spn_expect_success) {
    spn_expect(true);
    zassert_false(handler_called, "Handler should not be called for successful expectation");
}

#ifdef CONFIG_SPN_ASSERT_HANDLER
ZTEST(spn_debugging, test_spn_expect_failure_with_handler) {
    spn::debugging::set_spn_expect_handler(test_expect_handler);

    spn_expect(false);

    zassert_true(handler_called, "Handler should be called for failed expectation");
    zassert_not_null(last_condition, "Condition should be captured");
    zassert_not_null(last_file, "File should be captured");
    zassert_true(last_line > 0, "Line should be captured");

    spn::debugging::set_spn_expect_handler(nullptr);
}

ZTEST(spn_debugging, test_handler_registration) {
    spn::debugging::set_spn_assert_handler(test_assert_handler);
    spn::debugging::set_spn_expect_handler(test_expect_handler);

    spn::debugging::set_spn_assert_handler(nullptr);
    spn::debugging::set_spn_expect_handler(nullptr);

    zassert_true(true, "Handler registration should complete without error");
}

ZTEST(spn_debugging, test_assert_with_expressions) {
    int value = 42;
    spn_assert(value == 42);
    spn_assert(value > 0);
    spn_assert(value < 100);

    zassert_false(handler_called, "Handler should not be called for successful assertions");
}

ZTEST(spn_debugging, test_expect_with_expressions) {
    int value = 42;
    spn_expect(value == 42);
    spn_expect(value > 0);
    spn_expect(value < 100);

    zassert_false(handler_called, "Handler should not be called for successful expectations");
}

ZTEST(spn_debugging, test_expect_handler_callback) {
    spn::debugging::set_spn_expect_handler(test_expect_handler);

    spn_expect(1 == 2);

    zassert_true(handler_called, "Handler should be called for failed expectation");
    zassert_str_equal("1 == 2", last_condition, "Condition should be captured correctly");

    spn::debugging::set_spn_expect_handler(nullptr);
}
#endif