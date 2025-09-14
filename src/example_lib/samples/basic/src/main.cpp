#include "spn/example_lib/example_lib.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spn_example_sample, LOG_LEVEL_INF);

int main(void) {
    LOG_INF("SPN Example Library Sample");

    bool result = spn::example_lib::example_function();
    LOG_INF("example_function returned: %s", result ? "true" : "false");

    return 0;
}