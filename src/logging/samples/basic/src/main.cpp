#include <spn/logging/logging.hpp>
#include <spn/logging/memory.hpp>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spn_logging_sample, LOG_LEVEL_INF);

int main(void) {
    LOG_INF("SPN Logging Library Sample");

    // module-based logging macros
    MLOG_INF(spn_logging_sample, "Testing MLOG_INF macro");
    MLOG_WRN(spn_logging_sample, "Testing MLOG_WRN macro");
    MLOG_ERR(spn_logging_sample, "Testing MLOG_ERR macro");

    // once-only logging macros
    for (int i = 0; i < 3; i++) {
        MLOG_WRN_ONCE(spn_logging_sample, "This warning message should only appear once (iteration %d)", i);
    }

    // memory logging utilities
    LOG_STACK_SPACE(spn_logging_sample);
    LOG_SYS_HEAP_SPACE(spn_logging_sample);

    LOG_INF("Logging sample completed");
    return 0;
}