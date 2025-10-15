#pragma once

#include "spn/timing/monotonic_clock.hpp"

#include <etl/algorithm.h>
#include <zephyr/kernel.h>

namespace spn::timing {

/// Rate limiter using token bucket algorithm. Allows bursts up to bucket capacity. (ISR safe)
class RateLimiter {
public:
    /// Constructor with rate and bucket capacity
    explicit RateLimiter(uint32_t rate_per_second, uint32_t bucket_capacity = 0)
        : _rate_per_second(rate_per_second), _bucket_capacity(bucket_capacity > 0 ? bucket_capacity : rate_per_second),
          _tokens(_bucket_capacity), _last_refill_time(UptimeClock::now()) {}

    /// Attempt to acquire n tokens. Returns false when insufficient tokens available
    bool try_acquire(uint32_t n = 1) {
        refill_tokens();
        if (_tokens >= n) {
            _tokens -= n;
            return true;
        }
        return false;
    }

    /// Get current token count
    uint32_t available_tokens() const { return _tokens; }

    /// Get rate in operations per second
    uint32_t rate_per_second() const { return _rate_per_second; }

    /// Get bucket capacity
    uint32_t bucket_capacity() const { return _bucket_capacity; }

    /// Reset the rate limiter with full tokens
    void reset() {
        _tokens           = _bucket_capacity;
        _last_refill_time = UptimeClock::now();
    }

private:
    void refill_tokens() {
        auto now             = UptimeClock::now();
        auto time_elapsed_ms = to_ms(now - _last_refill_time);

        if (time_elapsed_ms > 0) {
            const uint64_t tokens_to_add = (static_cast<uint64_t>(time_elapsed_ms) * _rate_per_second) / 1000;
            if (tokens_to_add > 0) {
                _tokens           = etl::min<uint64_t>(_bucket_capacity, _tokens + tokens_to_add);
                _last_refill_time = now;
            }
        }
    }

    uint32_t                _rate_per_second;
    uint32_t                _bucket_capacity;
    uint32_t                _tokens;
    UptimeClock::time_point _last_refill_time;
};

/// Throttle utility to enforce minimum time between operations. (ISR safe)
class Throttle {
public:
    /// Constructor with minimum interval between operations
    explicit Throttle(const chrono::milliseconds& min_interval)
        : _min_interval(min_interval), _last_operation_time(UptimeClock::time_point{}) {}

    /// Check if operation is allowed. Returns true if enough time has passed since last operation
    bool allow() {
        auto now     = UptimeClock::now();
        auto elapsed = now - _last_operation_time;

        if (elapsed >= _min_interval) {
            _last_operation_time = now;
            return true;
        }
        return false;
    }

    /// Get time remaining until next operation is allowed
    chrono::milliseconds time_until_allowed() const {
        auto now     = UptimeClock::now();
        auto elapsed = now - _last_operation_time;

        if (elapsed >= _min_interval) {
            return chrono::milliseconds{0};
        }
        return chrono::duration_cast<chrono::milliseconds>(_min_interval - elapsed);
    }

    /// Get minimum interval
    chrono::milliseconds min_interval() const { return _min_interval; }

    /// Reset throttle to allow immediate operation
    void reset() { _last_operation_time = UptimeClock::time_point{}; }

private:
    chrono::milliseconds    _min_interval;
    UptimeClock::time_point _last_operation_time;
};

/// Deadline tracker for monitoring timing constraints. (ISR safe)
class Deadline {
public:
    /// Constructor with deadline duration
    explicit Deadline(const chrono::milliseconds& deadline) : _deadline(deadline) {}

    /// Start deadline tracking
    void start() {
        _start_time = UptimeClock::now();
        _active     = true;
    }

    /// Stop deadline tracking. Returns true if deadline was met, false if exceeded
    bool stop() {
        if (!_active) return true;

        _active      = false;
        auto elapsed = UptimeClock::now() - _start_time;
        return elapsed <= _deadline;
    }

    /// Check if deadline is exceeded without stopping. Returns true if deadline is exceeded
    bool is_exceeded() const {
        if (!_active) return false;

        auto elapsed = UptimeClock::now() - _start_time;
        return elapsed > _deadline;
    }

    /// Get elapsed time since start
    chrono::milliseconds elapsed() const {
        if (!_active) return chrono::milliseconds{0};
        return chrono::duration_cast<chrono::milliseconds>(UptimeClock::now() - _start_time);
    }

    /// Get time remaining until deadline
    chrono::milliseconds remaining() const {
        if (!_active) return chrono::milliseconds{0};

        auto elapsed = UptimeClock::now() - _start_time;
        if (elapsed >= _deadline) return chrono::milliseconds{0};

        return chrono::duration_cast<chrono::milliseconds>(_deadline - elapsed);
    }

    /// Get deadline duration
    chrono::milliseconds deadline() const { return _deadline; }

    /// Returns true if deadline tracking is active
    bool is_active() const { return _active; }

    /// Reset deadline to inactive state
    void reset() {
        _active     = false;
        _start_time = UptimeClock::time_point{};
    }

private:
    chrono::milliseconds    _deadline;
    UptimeClock::time_point _start_time{};
    bool                    _active{false};
};

/// RAII deadline tracker that automatically starts on construction. (ISR safe)
class ScopedDeadline {
public:
    /// Constructor that starts deadline tracking
    explicit ScopedDeadline(const chrono::milliseconds& deadline) : _deadline(deadline) { _deadline.start(); }

    /// Get reference to underlying deadline
    const Deadline& deadline() const { return _deadline; }

    /// Check if deadline is exceeded
    bool is_exceeded() const { return _deadline.is_exceeded(); }

private:
    Deadline _deadline;
};

} // namespace spn::timing
