#pragma once

#include <etl/chrono.h>
#include <etl/ratio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>

namespace spn::timing {

namespace chrono          = etl::chrono;
namespace chrono_literals = etl::chrono_literals;

/// Zephyr duration types for ticks and cycles
using Ticks  = chrono::duration<k_ticks_t, etl::ratio<1, CONFIG_SYS_CLOCK_TICKS_PER_SEC>>;
using Cycles = chrono::duration<uint64_t, etl::ratio<1>>;

/// Monotonic clock based on Zephyr uptime. Provides millisecond precision
class UptimeClock {
public:
    using duration   = chrono::milliseconds;
    using rep        = duration::rep;
    using period     = duration::period;
    using time_point = chrono::time_point<UptimeClock, duration>;

    static constexpr bool is_steady = true;

    /// Returns current uptime-based time point
    static time_point now() { return time_point(duration(k_uptime_get())); }

    /// Get time since system boot in milliseconds
    static uint64_t uptime_ms() { return k_uptime_get(); }

    /// Get time since system boot in ticks
    static k_ticks_t uptime_ticks() { return k_uptime_ticks(); }
};

/// Monotonic clock based on hardware cycle counter. Provides maximum precision
class CycleClock {
public:
    using duration   = Cycles;
    using rep        = duration::rep;
    using period     = duration::period;
    using time_point = chrono::time_point<CycleClock, duration>;

    static constexpr bool is_steady = true;

    /// Returns current cycle-based time point
    static time_point now() { return time_point(duration(cycles())); }

    /// Get current cycle count. Falls back to 32-bit counter when 64-bit unavailable
    static uint64_t cycles() {
        if constexpr (IS_ENABLED(CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER)) {
            return k_cycle_get_64();
        } else {
            return k_cycle_get_32();
        }
    }

    /// Get 32-bit cycle count for lower overhead. Wraps around more frequently
    static uint32_t cycles_32() { return k_cycle_get_32(); }

    /// Convert cycles to nanoseconds
    static uint64_t cycles_to_ns(uint64_t cycles) { return k_cyc_to_us_floor64(cycles) * 1000ULL; }

    /// Convert cycles to microseconds
    static uint64_t cycles_to_us(uint64_t cycles) { return k_cyc_to_us_floor64(cycles); }
};

/// Add time_point + duration operator (ETL doesn't provide this)
template<typename Clock, typename Duration, typename Rep, typename Period>
constexpr chrono::time_point<Clock, Duration>
operator+(const chrono::time_point<Clock, Duration>& tp, const chrono::duration<Rep, Period>& d) {
    return chrono::time_point<Clock, Duration>(tp.time_since_epoch() + d);
}

/// Add duration + time_point operator (ETL doesn't provide this)
template<typename Rep, typename Period, typename Clock, typename Duration>
constexpr chrono::time_point<Clock, Duration>
operator+(const chrono::duration<Rep, Period>& d, const chrono::time_point<Clock, Duration>& tp) {
    return tp + d;
}

/// Add time_point - duration operator (ETL doesn't provide this)
template<typename Clock, typename Duration, typename Rep, typename Period>
constexpr chrono::time_point<Clock, Duration>
operator-(const chrono::time_point<Clock, Duration>& tp, const chrono::duration<Rep, Period>& d) {
    return chrono::time_point<Clock, Duration>(tp.time_since_epoch() - d);
}

/// Add time_point - time_point operator (ETL doesn't provide this)
template<typename Clock, typename Duration1, typename Duration2>
constexpr typename etl::common_type<Duration1, Duration2>::type
operator-(const chrono::time_point<Clock, Duration1>& lhs, const chrono::time_point<Clock, Duration2>& rhs) {
    return lhs.time_since_epoch() - rhs.time_since_epoch();
}

/// Convert duration to milliseconds for logging/display. Use at Zephyr/logging boundaries only
template<typename Rep, typename Period>
constexpr int64_t to_ms(const chrono::duration<Rep, Period>& d) {
    return chrono::duration_cast<chrono::milliseconds>(d).count();
}

/// Convert duration to microseconds for logging/display. Use at Zephyr/logging boundaries only
template<typename Rep, typename Period>
constexpr int64_t to_us(const chrono::duration<Rep, Period>& d) {
    return chrono::duration_cast<chrono::microseconds>(d).count();
}

/// Convert duration to nanoseconds for logging/display. Use at Zephyr/logging boundaries only
template<typename Rep, typename Period>
constexpr int64_t to_ns(const chrono::duration<Rep, Period>& d) {
    return chrono::duration_cast<chrono::nanoseconds>(d).count();
}

/// Convert chrono duration to Zephyr timeout. Clamps to UINT32_MAX ms. Use at Zephyr API boundaries only
template<typename Rep, typename Period>
k_timeout_t to_timeout(const chrono::duration<Rep, Period>& d) {
    auto    ms       = chrono::duration_cast<chrono::milliseconds>(d);
    int64_t ms_count = ms.count();

    __ASSERT(ms_count >= 0, "timeout duration must be non-negative");
    __ASSERT(
        ms_count <= static_cast<int64_t>(UINT32_MAX),
        "timeout duration exceeds UINT32_MAX milliseconds (~50 days)"
    );

    uint32_t clamped = (ms_count > static_cast<int64_t>(UINT32_MAX)) ? UINT32_MAX
                       : (ms_count < 0)                              ? 0
                                                                     : static_cast<uint32_t>(ms_count);
    return K_MSEC(clamped);
}

/// Sleep for specified duration. Uses k_sleep for milliseconds, k_busy_wait for microseconds, cycles for
/// sub-microsecond
template<typename Rep, typename Period>
void sleep_for(const chrono::duration<Rep, Period>& d) {
    auto ms = chrono::duration_cast<chrono::milliseconds>(d);
    if (ms.count() > 0) {
        constexpr chrono::milliseconds max_chunk{INT32_MAX};
        auto                           remaining = ms;
        while (remaining > chrono::milliseconds::zero()) {
            auto chunk = etl::min(remaining, max_chunk);
            k_sleep(K_MSEC(chunk.count()));
            remaining -= chunk;
        }
    }

    auto remaining_us = chrono::duration_cast<chrono::microseconds>(d - ms);
    if (remaining_us.count() > 0) {
        k_busy_wait(static_cast<uint32_t>(remaining_us.count()));
        return;
    }

    auto ns = chrono::duration_cast<chrono::nanoseconds>(d);
    if (ns.count() <= 0) {
        return;
    }

#if defined(CONFIG_ARCH_POSIX)
    k_busy_wait(1);
#else
    uint64_t target_cycles = k_ns_to_cyc_ceil64(static_cast<uint64_t>(ns.count()));
    if (target_cycles == 0) {
        return;
    }

    uint64_t start_cycles = CycleClock::cycles();
    while ((CycleClock::cycles() - start_cycles) < target_cycles) {
        // busy wait
    }
#endif
}

/// Sleep until specified time point
template<typename Clock, typename Duration>
void sleep_until(const chrono::time_point<Clock, Duration>& time_point) {
    auto now = Clock::now();
    if (time_point > now) {
        sleep_for(time_point - now);
    }
}

} // namespace spn::timing
