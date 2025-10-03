#pragma once

#include "spn/threading/pacing/pacing_strategy.hpp"

#include <zephyr/kernel.h>

namespace spn::threading {

/// Pacing strategy driven by a Zephyr timer and semaphore
/// note: supports above 1kHz pacing
class TimerPacing : public IPacingStrategy {
public:
    TimerPacing(k_timeout_t period, k_timeout_t initial_delay = K_NO_WAIT);

    void on_enter_running() override;

    void on_exit_running() override;

    void wait() override;

    void interrupt() override;

private:
    static void timer_entry(k_timer* timer);

    void handle_tick();

    k_timer     _timer{};
    k_sem       _tick_gate{};
    k_timeout_t _initial_delay;
    k_timeout_t _period;
    bool        _running = false;
};

} // namespace spn::threading
