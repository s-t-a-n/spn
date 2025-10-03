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

void __spn_log_expect_failure(const char* condition, const char* file, int line);
void __spn_log_assert_failure(const char* condition, const char* file, int line);

} // namespace spn::debugging

#if CONFIG_SPN_ASSERT_LEVEL > 0

#    ifdef CONFIG_SPN_ASSERT_HANDLER
#        define spn_assert(condition)                                                                                  \
            do {                                                                                                       \
                if (!(condition)) {                                                                                    \
                    auto handler = spn::debugging::spn_assert_handler();                                               \
                    if (handler) {                                                                                     \
                        handler(__FILE__, __LINE__, #condition, "Assertion failed");                                   \
                    } else if (IS_ENABLED(CONFIG_ASSERT)) {                                                            \
                        __ASSERT(condition, "spn_assert failed: %s", #condition);                                      \
                    } else {                                                                                           \
                        spn::debugging::__spn_log_assert_failure(#condition, __FILE__, __LINE__);                      \
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
                        spn::debugging::__spn_log_expect_failure(#condition, __FILE__, __LINE__);                      \
                    }                                                                                                  \
                }                                                                                                      \
            } while (0)
#    else
#        define spn_assert(condition) __ASSERT(condition, "spn_assert failed: %s", #condition)

#        define spn_expect(condition)                                                                                  \
            do {                                                                                                       \
                if (!(condition)) {                                                                                    \
                    spn::debugging::__spn_log_expect_failure(#condition, __FILE__, __LINE__);                          \
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