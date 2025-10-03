#include "spn/threading/pacing/throttle_pacing.hpp"

namespace spn::threading {

ThrottlePacing::ThrottlePacing(uint32_t min_interval_ms)
    : _min_interval_ms(min_interval_ms), _throttle(min_interval_ms), _interrupt_requested(false) {}

void ThrottlePacing::on_enter_running() {
    _interrupt_requested.store(false);
    _throttle.reset();
}

void ThrottlePacing::on_exit_running() {
    _interrupt_requested.store(false);
    _throttle.reset();
}

void ThrottlePacing::wait() {
    while (true) {
        if (_interrupt_requested.exchange(false)) {
            return;
        }

        if (_throttle.allow()) {
            return;
        }

        if (const uint32_t remaining = _throttle.time_until_allowed(); remaining == 0) {
            k_yield();
        } else {
            k_sleep(K_MSEC(remaining));
        }
    }
}

void ThrottlePacing::after_iteration() {
    if (_min_interval_ms == 0) {
        k_yield();
    }
}

void ThrottlePacing::interrupt() { _interrupt_requested.store(true); }

} // namespace spn::threading
