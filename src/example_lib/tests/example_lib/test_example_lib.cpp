#include "spn/example_lib/example_lib.hpp"

#include <zephyr/ztest.h>

ZTEST_SUITE(example_lib_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(example_lib_suite, test_example_function) {
    zassert_true(spn::example_lib::example_function(), "example_function should return true");
}
