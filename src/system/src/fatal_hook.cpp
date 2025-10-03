#include "spn/system/shutdown.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spn_fatal, LOG_LEVEL_INF);

extern "C" void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf* esf) {
    ARG_UNUSED(esf);

    LOG_ERR("fatal error: reason %u", reason);
    LOG_ERR("halting system");
    k_fatal_halt(reason);
}
