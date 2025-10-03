#include "spn/threading/pacing/timer_pacing.hpp"

namespace spn::threading {

TimerPacing::TimerPacing(k_timeout_t period, k_timeout_t initial_delay)
    : _initial_delay(initial_delay), _period(period) {
    k_timer_init(&_timer, timer_entry, nullptr);
    k_timer_user_data_set(&_timer, this);
    k_sem_init(&_tick_gate, 0, 1);
}

void TimerPacing::on_enter_running() {
    if (_running) return;

    _running = true;
    k_sem_reset(&_tick_gate);
    k_timer_start(&_timer, _initial_delay, _period);
}

void TimerPacing::on_exit_running() {
    if (!_running) return;

    _running = false;
    k_timer_stop(&_timer);
    k_sem_give(&_tick_gate);
}

void TimerPacing::wait() { k_sem_take(&_tick_gate, K_FOREVER); }

void TimerPacing::interrupt() { k_sem_give(&_tick_gate); }

void TimerPacing::timer_entry(k_timer* timer) {
    if (auto* pacing = static_cast<TimerPacing*>(k_timer_user_data_get(timer)); pacing != nullptr) {
        pacing->handle_tick();
    }
}

void TimerPacing::handle_tick() { k_sem_give(&_tick_gate); }

} // namespace spn::threading
