#include "spn/debugging/assert.hpp"

#include "spn/logging/logging.hpp"
#include "spn/system/exception.hpp"

#include <zephyr/init.h>
#include <zephyr/kernel.h>

LOG_MODULE_DECLARE(spn_system);

namespace spn::system {

#ifdef CONFIG_SPN_ASSERT_HANDLER
static void system_assert_handler(const char* file, int line, const char* condition, const char* message) {
    MLOG_ERR(spn_system, "assertion failed: %s at %s:%i - %s", condition, file, line, message);

    assertion_exception ex(condition);
    spn::throw_exception(ex);
}
#endif

void enable_assert_exceptions() {
#ifdef CONFIG_SPN_ASSERT_HANDLER
    spn::debugging::set_spn_assert_handler(system_assert_handler);
    MLOG_INF(spn_system, "registered system as assert handler");
#else
    MLOG_WRN(spn_system, "CONFIG_SPN_ASSERT_HANDLER not enabled, cannot set exception handler");
#endif
}

#ifdef CONFIG_SPN_AUTO_EXCEPTION_ASSERTS
static int auto_register_assert_handler(void) {
    enable_assert_exceptions();
    return 0;
}

SYS_INIT(auto_register_assert_handler, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#endif

} // namespace spn::system