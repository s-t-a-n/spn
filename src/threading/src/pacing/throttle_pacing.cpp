#include "spn/threading/pacing/throttle_pacing.hpp"

namespace spn::threading {

ThrottlePacing::ThrottlePacing(timing::chrono::milliseconds min_interval)
    : _min_interval(min_interval), _throttle(min_interval), _interrupt_requested(false) {}

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

        auto remaining = _throttle.time_until_allowed();
        if (remaining.count() == 0) {
            k_yield();
        } else {
            k_sleep(timing::to_timeout(remaining));
        }
    }
}

void ThrottlePacing::after_iteration() {
    if (_min_interval.count() == 0) {
        k_yield();
    }
}

void ThrottlePacing::interrupt() { _interrupt_requested.store(true); }

} // namespace spn::threading
