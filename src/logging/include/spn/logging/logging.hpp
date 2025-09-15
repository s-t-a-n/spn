#pragma once
#include <zephyr/logging/log.h>

namespace spn::logging {

#define MLOG_DBG(MODULE, ...)                                                                                          \
    do {                                                                                                               \
        LOG_MODULE_DECLARE(MODULE);                                                                                    \
        LOG_DBG(__VA_ARGS__);                                                                                          \
    } while (0)
#define MLOG_INF(MODULE, ...)                                                                                          \
    do {                                                                                                               \
        LOG_MODULE_DECLARE(MODULE);                                                                                    \
        LOG_INF(__VA_ARGS__);                                                                                          \
    } while (0)
#define MLOG_WRN(MODULE, ...)                                                                                          \
    do {                                                                                                               \
        LOG_MODULE_DECLARE(MODULE);                                                                                    \
        LOG_WRN(__VA_ARGS__);                                                                                          \
    } while (0)
#define MLOG_ERR(MODULE, ...)                                                                                          \
    do {                                                                                                               \
        LOG_MODULE_DECLARE(MODULE);                                                                                    \
        LOG_ERR(__VA_ARGS__);                                                                                          \
    } while (0)

#define MLOG_WRN_ONCE(MODULE, ...)                                                                                     \
    do {                                                                                                               \
        LOG_MODULE_DECLARE(MODULE);                                                                                    \
        LOG_WRN_ONCE(__VA_ARGS__);                                                                                     \
    } while (0)

} // namespace spn::logging