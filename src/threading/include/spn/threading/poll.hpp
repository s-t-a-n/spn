#pragma once

#include <zephyr/kernel.h>

namespace spn {

/// Lightweight Zephyr k_poll wrapper with signal and event. Provides edge-triggered notification between threads or
/// ISRs. note: signal() is ISR-safe. wait() is not ISR-safe. concurrent signal() and wait() require external
/// synchronization.
class Poll {
public:
    Poll() { init(); }

    /// Initialize poll signal and event
    void init() {
        k_poll_signal_init(&_sig);
        k_poll_event_init(&_ev, K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &_sig);
    }

    /// Raise signal to wake waiters. Returns 0 on success or error code.
    int signal(int value = 1) { return k_poll_signal_raise(&_sig, value); }

    /// Wait for signal or timeout. Returns 0 when signaled, -EAGAIN on timeout, -EINTR if interrupted, or other error
    /// code.
    int wait(k_timeout_t timeout) {
        _ev.state = K_POLL_STATE_NOT_READY;
        int rc    = k_poll(&_ev, 1, timeout);

        if (_ev.state == K_POLL_STATE_SIGNALED) {
            k_poll_signal_reset(&_sig);
            return 0;
        }

        return rc;
    }

private:
    k_poll_event  _ev{};
    k_poll_signal _sig{};
};

} // namespace spn
