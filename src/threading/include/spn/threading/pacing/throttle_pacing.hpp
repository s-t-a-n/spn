#pragma once

#include "spn/threading/pacing/pacing_strategy.hpp"
#include "spn/timing/rate_control.hpp"

#include <etl/atomic.h>
#include <zephyr/kernel.h>

namespace spn::threading {

/// Pacing strategy that enforces a minimum interval via spn::timing::Throttle
/// note: supports up to 1kHz pacing
class ThrottlePacing : public IPacingStrategy {
public:
    explicit ThrottlePacing(uint32_t min_interval_ms);

    void on_enter_running() override;

    void on_exit_running() override;

    void wait() override;

    void after_iteration() override;

    void interrupt() override;

private:
    uint32_t          _min_interval_ms;
    timing::Throttle  _throttle;
    etl::atomic<bool> _interrupt_requested;
};

} // namespace spn::threading
