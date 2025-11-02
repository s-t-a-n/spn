#pragma once

#include <zephyr/kernel.h>

namespace spn::system {

/// reasons for system shutdown
enum class shutdown_reason : uint8_t {
    unknown = 0,
    user_request,
    config_error,
    network_failure,
    fatal_assert,
    fatal_oops,
    exception_thrown,
};

/// shutdown manager interface for graceful system shutdown
class ShutdownManager {
public:
    virtual ~ShutdownManager() = default;

    ShutdownManager()                                      = default;
    ShutdownManager(const ShutdownManager&)                = delete;
    ShutdownManager& operator=(const ShutdownManager&)     = delete;
    ShutdownManager(ShutdownManager&&) noexcept            = delete;
    ShutdownManager& operator=(ShutdownManager&&) noexcept = delete;

    /// request shutdown with specified reason
    virtual void request_shutdown(shutdown_reason reason) = 0;
};

/// set global shutdown manager
/// note: returns previous manager for restoration
ShutdownManager* set_shutdown_manager(ShutdownManager* mgr);

/// get current shutdown manager
ShutdownManager* shutdown_manager();

/// request cooperative shutdown from any context
/// note: defers work to system work queue for thread safety
void request_shutdown(shutdown_reason reason);

/// final shutdown step when teardown is complete
/// note: calls exit() on native_sim or sys_reboot() on hardware
[[noreturn]] void finalize_shutdown();

#ifdef CONFIG_ZTEST
/// internal function for testing only
void reset_shutdown_state();
#endif

} // namespace spn::system
