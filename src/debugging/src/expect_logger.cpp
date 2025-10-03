#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spn_expect);

namespace spn::debugging {

void __spn_log_expect_failure(const char* condition, const char* file, int line) {
    LOG_WRN("failed: '%s' at %s:%d", condition, file, line);
}

} // namespace spn::debugging
