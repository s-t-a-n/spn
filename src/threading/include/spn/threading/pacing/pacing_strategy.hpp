#pragma once

#include <cstdint>

namespace spn {

enum class ThreadState : uint32_t;

} // namespace spn

namespace spn::threading {

/// Thread pacing strategy interface
class IPacingStrategy {
public:
    virtual ~IPacingStrategy() = default;

    IPacingStrategy()                                      = default;
    IPacingStrategy(const IPacingStrategy&)                = delete;
    IPacingStrategy& operator=(const IPacingStrategy&)     = delete;
    IPacingStrategy(IPacingStrategy&&) noexcept            = delete;
    IPacingStrategy& operator=(IPacingStrategy&&) noexcept = delete;

    /// Called before the thread enters its running loop
    virtual void on_enter_running() {}

    /// Called after the thread leaves its running loop
    virtual void on_exit_running() {}

    /// Block until the next delegate iteration is allowed
    virtual void wait() = 0;

    /// Called after the thread delegate just finished
    virtual void after_iteration() {}

    /// Interrupt any ongoing wait so the thread can react to state changes
    virtual void interrupt() {}
};

} // namespace spn::threading
