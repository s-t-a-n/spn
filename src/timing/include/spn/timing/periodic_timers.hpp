#pragma once

#include "spn/timing/monotonic_clock.hpp"

#include <etl/delegate.h>
#include <etl/utility.h>
#include <etl/variant.h>
#include <zephyr/kernel.h>

namespace spn::timing {

/// Callback type for timer expiration events
using timer_callback_f = etl::delegate<void()>;

/// One-shot timer that fires once after a specified duration. Callback executed from ISR
class OneShotTimer {
public:
    OneShotTimer() = default;
    ~OneShotTimer() { stop(); }

    OneShotTimer(const OneShotTimer&)            = delete;
    OneShotTimer& operator=(const OneShotTimer&) = delete;
    OneShotTimer(OneShotTimer&&)                 = delete;
    OneShotTimer& operator=(OneShotTimer&&)      = delete;

    /// Start the timer with specified duration and callback
    void start(const chrono::milliseconds& duration, timer_callback_f callback) {
        stop(); // Stop any existing timer

        _callback = etl::move(callback);
        k_timer_init(&_timer, timer_expiry_isr, nullptr);
        k_timer_user_data_set(&_timer, this);
        k_timer_start(&_timer, to_timeout(duration), K_NO_WAIT);
        _initialized = true;
    }

    /// Stop the timer if it's running
    void stop() {
        if (_initialized) {
            k_timer_stop(&_timer);
            k_timer_status_sync(&_timer);
            _initialized = false;
        }
    }

    /// Returns true if timer is running
    bool is_running() const { return k_timer_remaining_ticks(&_timer) > 0; }

    /// Get remaining time
    chrono::milliseconds remaining() const {
        const k_ticks_t ticks = k_timer_remaining_ticks(&_timer);
        const uint64_t  ms    = k_ticks_to_ms_floor64(ticks);
        return chrono::milliseconds(ms > INT64_MAX ? INT64_MAX : static_cast<int64_t>(ms));
    }

private:
    static void timer_expiry_isr(k_timer* timer) {
        auto* self = static_cast<OneShotTimer*>(k_timer_user_data_get(timer));
        if (self && self->_callback) self->_callback();
    }

    k_timer          _timer = {};
    timer_callback_f _callback;
    bool             _initialized = false;
};

/// Periodic timer that fires repeatedly at specified intervals. Callback executed from ISR
class PeriodicTimer {
public:
    PeriodicTimer() = default;
    ~PeriodicTimer() { stop(); }

    PeriodicTimer(const PeriodicTimer&)            = delete;
    PeriodicTimer& operator=(const PeriodicTimer&) = delete;
    PeriodicTimer(PeriodicTimer&&)                 = delete;
    PeriodicTimer& operator=(PeriodicTimer&&)      = delete;

    /// Start the timer with specified period and callback
    void start(const chrono::milliseconds& period, timer_callback_f callback) {
        start(chrono::milliseconds{0}, period, etl::move(callback));
    }

    /// Start the timer with initial delay and different period
    void
    start(const chrono::milliseconds& initial_delay, const chrono::milliseconds& period, timer_callback_f callback) {
        stop(); // stop any existing timer

        _period   = period;
        _callback = etl::move(callback);
        k_timer_init(&_timer, timer_expiry_isr, nullptr);
        k_timer_user_data_set(&_timer, this);
        k_timer_start(&_timer, to_timeout(initial_delay), to_timeout(period));
        _initialized = true;
    }

    /// Stop the timer
    void stop() {
        if (_initialized) {
            k_timer_stop(&_timer);
            k_timer_status_sync(&_timer);
            _initialized = false;
        }
    }

    /// Returns true if timer is running
    bool is_running() const { return k_timer_remaining_ticks(&_timer) > 0; }

    /// Get remaining time until next expiry
    chrono::milliseconds remaining() const {
        const k_ticks_t ticks = k_timer_remaining_ticks(&_timer);
        const uint64_t  ms    = k_ticks_to_ms_floor64(ticks);
        return chrono::milliseconds(ms > INT64_MAX ? INT64_MAX : static_cast<int64_t>(ms));
    }

    /// Get timer period
    chrono::milliseconds period() const { return _period; }

private:
    static void timer_expiry_isr(k_timer* timer) {
        auto* self = static_cast<PeriodicTimer*>(k_timer_user_data_get(timer));
        if (self && self->_callback) self->_callback();
    }

    k_timer              _timer = {};
    timer_callback_f     _callback;
    chrono::milliseconds _period{};
    bool                 _initialized = false;
};

} // namespace spn::timing
