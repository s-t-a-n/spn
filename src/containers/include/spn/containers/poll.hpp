#pragma once

#include <zephyr/kernel.h>

namespace spn {

/// Lightweight Zephyr k_poll wrapper with signal and event
class Poll {
public:
    Poll() { init(); }

    /// Initialize poll signal and event
    void init() {
        k_poll_signal_init(&_sig);
        k_poll_event_init(&_ev, K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &_sig);
    }

    /// Raise signal to wake waiters
    void signal(int value = 1) { k_poll_signal_raise(&_sig, value); }

    /// Wait for signal or timeout
    /// note: returns false on timeout
    bool wait(k_timeout_t timeout) {
        (void)k_poll(&_ev, 1, timeout);
        unsigned int signaled = 0;
        int          result   = 0;
        k_poll_signal_check(&_sig, &signaled, &result);
        if (signaled) {
            k_poll_signal_reset(&_sig);
        }
        return signaled != 0;
    }

private:
    k_poll_event  _ev{};
    k_poll_signal _sig{};
};

} // namespace spn
