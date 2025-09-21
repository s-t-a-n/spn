#include "spn/system/shutdown.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spn_fatal, LOG_LEVEL_INF);

extern "C" void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf* esf) {
    ARG_UNUSED(esf);

    LOG_ERR("fatal error: reason %u", reason);

    if (spn::system::shutdown_manager() != NULL) {
        LOG_INF("attempting cooperative shutdown");
        spn::system::request_shutdown(spn::system::shutdown_reason::fatal_oops);
        k_sleep(K_MSEC(100));
    }

    LOG_ERR("halting system");
    k_fatal_halt(reason);
}