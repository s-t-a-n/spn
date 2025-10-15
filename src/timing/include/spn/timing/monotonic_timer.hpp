#pragma once

#include "spn/timing/monotonic_clock.hpp"

namespace spn::timing {

/// Utility class for measuring elapsed time with automatic clock selection
template<typename Clock = UptimeClock>
class MonotonicTimer {
public:
    using clock      = Clock;
    using time_point = typename Clock::time_point;
    using duration   = typename Clock::duration;

    MonotonicTimer() = default;

    /// Start timing
    void start() {
        _start_time = Clock::now();
        _is_running = true;
    }

    /// Stop timing and return elapsed duration
    duration stop() {
        if (!_is_running) return duration{};
        auto end_time = Clock::now();
        _elapsed      = end_time - _start_time;
        _is_running   = false;
        return _elapsed;
    }

    /// Get elapsed duration without stopping. Returns duration since start or last measured duration
    duration elapsed() const {
        if (_is_running) return Clock::now() - _start_time;
        return _elapsed;
    }

    /// Returns true if timer is running
    bool is_running() const { return _is_running; }

    /// Reset timer to stopped state
    void reset() {
        _is_running = false;
        _elapsed    = duration{};
    }

private:
    time_point _start_time{};
    duration   _elapsed{};
    bool       _is_running = false;
};

/// Type aliases for common clock types
using UptimeTimer = MonotonicTimer<UptimeClock>;
using CycleTimer  = MonotonicTimer<CycleClock>;

} // namespace spn::timing
