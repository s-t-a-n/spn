#include "spn/debugging/assert.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spn_debugging_sample, LOG_LEVEL_INF);

static void custom_assert_handler(const char* file, int line, const char* condition, const char* message) {
    LOG_ERR("Custom Assert Handler: %s at %s:%d - %s", condition, file, line, message);
}

static void custom_expect_handler(const char* file, int line, const char* condition, const char* message) {
    LOG_WRN("Custom Expect Handler: %s at %s:%d - %s", condition, file, line, message);
}

int main(void) {
    LOG_INF("SPN Debugging Assert Sample");

    LOG_INF("Testing basic use of spn_assert/expect");
    spn_assert(true == true);
    spn_expect(true == true);

    LOG_INF("Registering custom handlers...");
    spn::debugging::set_spn_assert_handler(custom_assert_handler);
    spn::debugging::set_spn_expect_handler(custom_expect_handler);

    LOG_INF("Testing expectation failure with custom handler...");
    spn_expect(1 == 2);
    LOG_INF("Expectation failure handled by custom handler");

    LOG_INF("Resetting handlers...");
    spn::debugging::set_spn_assert_handler(nullptr);
    spn::debugging::set_spn_expect_handler(nullptr);

    LOG_INF("Testing expectation without handler...");
    spn_expect(false);
    LOG_INF("Expectation without handler completed");

    LOG_INF("Assert sample completed successfully");
    return 0;
}
