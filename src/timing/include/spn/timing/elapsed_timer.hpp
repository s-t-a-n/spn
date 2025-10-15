#pragma once

#include <zephyr/kernel.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/timing/timing.h>

namespace spn::timing {

/// Basic elapsed time measurement for embedded targets. DEPRECATED: Use MonotonicTimer<UptimeClock> instead
class [[deprecated("Use MonotonicTimer<UptimeClock> instead")]] ElapsedTimer {
public:
    /// Start timing measurement
    void start() {
        _start = k_uptime_get();
        _end   = 0;
    }
    /// Stop timing measurement
    void stop() { _end = k_uptime_get(); }

    /// Get elapsed time in nanoseconds
    uint64_t elapsed_ns() const { return elapsed_ms() * 1000000ULL; }

    /// Get elapsed time in microseconds
    uint64_t elapsed_us() const { return elapsed_ms() * 1000ULL; }

    /// Get elapsed time in milliseconds
    uint64_t elapsed_ms() const {
        auto end = (_end != 0) ? _end : k_uptime_get();
        return end - _start;
    }

    /// Get elapsed time in seconds
    uint64_t elapsed_s() const { return elapsed_ms() / 1000ULL; }

private:
    uint64_t _start{};
    uint64_t _end{};
};

} // namespace spn::timing