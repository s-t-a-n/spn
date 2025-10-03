#include <spn/debugging/assert.hpp>

namespace spn::debugging {

#ifdef CONFIG_SPN_ASSERT_HANDLER
static assert_handler_f g_assert_handler = nullptr;
static expect_handler_f g_expect_handler = nullptr;

void set_spn_assert_handler(assert_handler_f handler) { g_assert_handler = handler; }

void set_spn_expect_handler(expect_handler_f handler) { g_expect_handler = handler; }

assert_handler_f spn_assert_handler() { return g_assert_handler; }

expect_handler_f spn_expect_handler() { return g_expect_handler; }
#endif

} // namespace spn::debugging