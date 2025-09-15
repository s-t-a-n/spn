#pragma once

#include "spn/logging/logging.hpp"

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/sys/util.h>

extern k_heap _system_heap;

namespace spn::logging {

#ifdef CONFIG_THREAD_STACK_INFO
#    define LOG_STACK_SPACE(log_module)                                                                                \
        do {                                                                                                           \
            size_t _free;                                                                                              \
            if (k_thread_stack_space_get(k_current_get(), &_free) == 0) {                                              \
                MLOG_INF(log_module, "stack: free: %u", _free);                                                        \
            } else {                                                                                                   \
                MLOG_ERR(log_module, "failed to read stack memory height");                                            \
            }                                                                                                          \
        } while (0)
#else
#    define LOG_STACK_SPACE(log_module)                                                                                \
        do {                                                                                                           \
            MLOG_WRN_ONCE(log_module, "stack monitoring not available on this platform");                              \
        } while (0)
#endif

#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
#    define LOG_SYS_HEAP_SPACE(log_module)                                                                             \
        do {                                                                                                           \
            struct sys_memory_stats _ms;                                                                               \
            if (sys_heap_runtime_stats_get(&_system_heap.heap, &_ms) == 0) {                                           \
                MLOG_INF(                                                                                              \
                    log_module,                                                                                        \
                    "sys heap: free: %u used: %u hiwat: %u",                                                           \
                    (uint32_t)_ms.free_bytes,                                                                          \
                    (uint32_t)_ms.allocated_bytes,                                                                     \
                    (uint32_t)_ms.max_allocated_bytes                                                                  \
                );                                                                                                     \
            } else {                                                                                                   \
                MLOG_ERR(log_module, "couldn't read sys heap stats");                                                  \
            }                                                                                                          \
        } while (0)
#else
#    define LOG_SYS_HEAP_SPACE(log_module)                                                                             \
        do {                                                                                                           \
            MLOG_WRN_ONCE(log_module, "sys heap monitoring not available on this platform");                           \
        } while (0)
#endif

} // namespace spn::logging
