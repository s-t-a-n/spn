#include <spn/logging/logging.hpp>
#include <spn/logging/memory.hpp>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_logging, LOG_LEVEL_INF);

ZTEST_SUITE(logging_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(logging_suite, test_mlog_macros) {
    // test that MLOG macros can be called without crashing
    MLOG_INF(test_logging, "Testing MLOG_INF in unit test");
    MLOG_DBG(test_logging, "Testing MLOG_DBG in unit test");
    MLOG_WRN(test_logging, "Testing MLOG_WRN in unit test");
    MLOG_ERR(test_logging, "Testing MLOG_ERR in unit test");
    MLOG_WRN_ONCE(test_logging, "Testing MLOG_WRN_ONCE in unit test");
    // ff we reach here without crashing, the test passes
}

ZTEST(logging_suite, test_memory_logging) {
    // memory logging macros
    LOG_STACK_SPACE(test_logging);
    LOG_SYS_HEAP_SPACE(test_logging);
    // ff we reach here without crashing, the test passes
}