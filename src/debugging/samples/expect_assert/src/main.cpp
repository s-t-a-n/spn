#include "spn/debugging/assert.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spn_debugging_sample, LOG_LEVEL_INF);

static void custom_expect_handler(const char* file, int line, const char* condition, const char* message) {
    LOG_WRN("Custom Expect Handler: %s at %s:%d - %s", condition, file, line, message);
}

static void custom_assert_handler(const char* file, int line, const char* condition, const char* message) {
    LOG_ERR("Custom Assert Handler: %s at %s:%d - %s", condition, file, line, message);
}

void expect_without_handler() {
    LOG_INF("=== spn_expect without handler ===");
    spn_expect(true == true);
    spn_expect(1 == 2);
    LOG_INF("Completed");
    printk("\n");
}

void expect_with_handler() {
    LOG_INF("=== spn_expect with custom handler ===");
    spn::debugging::set_spn_expect_handler(custom_expect_handler);
    spn_expect(true == true);
    spn_expect(1 == 2);
    spn::debugging::set_spn_expect_handler(nullptr);
    LOG_INF("Completed");
    printk("\n");
}

void assert_with_handler() {
    LOG_INF("=== spn_assert with custom handler ===");
    spn::debugging::set_spn_assert_handler(custom_assert_handler);
    spn_assert(true == true);
    spn_assert(1 == 2);
    spn::debugging::set_spn_assert_handler(nullptr);
    LOG_INF("Completed");
    printk("\n");
}

void assert_without_handler() {
    LOG_INF("=== spn_assert without handler ===");
    spn_assert(true == true);
    spn_assert(1 == 2);
    LOG_ERR("This line only executes if CONFIG_ASSERT was not set");
}

int main(void) {
    LOG_INF("SPN Debugging Expect/Assert Sample");

    expect_with_handler();
    expect_without_handler();
    assert_with_handler();
    assert_without_handler();

    LOG_INF("Sample completed");
    return 0;
}
