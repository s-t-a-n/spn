#include <spn/debugging/assert.hpp>
#include <zephyr/ztest.h>

ZTEST_SUITE(spn_debugging_no_handler, NULL, NULL, NULL, NULL, NULL);

ZTEST(spn_debugging_no_handler, test_assert_with_expressions) {
    int value = 42;
    spn_assert(value == 42);
    spn_assert(value > 0 && value <= 100);

    // no test for false condition, since deadtesting will end the process
}

ZTEST(spn_debugging_no_handler, test_expect_with_expressions) {
    int value = 42;
    spn_expect(value == 42);
    spn_expect(value > 0 && value <= 100);

    spn_expect(false);
    spn_expect(value < 0 || value > 100);
}
