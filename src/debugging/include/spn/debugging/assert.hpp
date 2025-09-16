#pragma once

#include "spn/logging/logging.hpp"

#include <zephyr/sys/__assert.h>

#ifdef spn_assert
#    error "spn_assert is already defined"
#endif

#ifdef spn_expect
#    error "spn_expect is already defined"
#endif

namespace spn::debugging {

using assert_handler_f = void (*)(const char* file, int line, const char* condition, const char* message);
using expect_handler_f = void (*)(const char* file, int line, const char* condition, const char* message);

#ifdef CONFIG_SPN_ASSERT_HANDLER
void             set_spn_assert_handler(assert_handler_f handler);
void             set_spn_expect_handler(expect_handler_f handler);
assert_handler_f spn_assert_handler();
expect_handler_f spn_expect_handler();
#endif

} // namespace spn::debugging

#if CONFIG_SPN_ASSERT_LEVEL > 0

#    ifdef CONFIG_SPN_ASSERT_HANDLER
#        define spn_assert(condition)                                                                                  \
            do {                                                                                                       \
                if (!(condition)) {                                                                                    \
                    auto handler = spn::debugging::spn_assert_handler();                                               \
                    if (handler) {                                                                                     \
                        handler(__FILE__, __LINE__, #condition, "Assertion failed");                                   \
                    } else {                                                                                           \
                        __ASSERT(condition, "spn_assert failed: %s", #condition);                                      \
                    }                                                                                                  \
                }                                                                                                      \
            } while (0)

#        define spn_expect(condition)                                                                                  \
            do {                                                                                                       \
                if (!(condition)) {                                                                                    \
                    auto handler = spn::debugging::spn_expect_handler();                                               \
                    if (handler) {                                                                                     \
                        handler(__FILE__, __LINE__, #condition, "Expectation failed");                                 \
                    } else {                                                                                           \
                        MLOG_WRN(spn_debugging, "spn_expect failed: %s at %s:%d", #condition, __FILE__, __LINE__);     \
                    }                                                                                                  \
                }                                                                                                      \
            } while (0)
#    else
#        define spn_assert(condition) __ASSERT(condition, "spn_assert failed: %s", #condition)

#        define spn_expect(condition)                                                                                  \
            do {                                                                                                       \
                if (!(condition)) {                                                                                    \
                    MLOG_WRN(spn_debugging("spn_expect failed: %s at %s:%d", #condition, __FILE__, __LINE__);          \
                }                                                                                                      \
            } while (0)
#    endif

#else
#    define spn_assert(condition)                                                                                      \
        do {                                                                                                           \
        } while (0)
#    define spn_expect(condition)                                                                                      \
        do {                                                                                                           \
        } while (0)
#endif