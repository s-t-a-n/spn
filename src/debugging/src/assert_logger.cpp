#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spn_assert);

namespace spn::debugging {

void __spn_log_assert_failure(const char* condition, const char* file, int line) {
    LOG_ERR("failed: '%s' at %s:%d", condition, file, line);
}

} // namespace spn::debugging
