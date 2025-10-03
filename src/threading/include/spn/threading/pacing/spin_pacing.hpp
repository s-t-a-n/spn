#pragma once

#include "spn/threading/pacing/pacing_strategy.hpp"

#include <zephyr/kernel.h>

namespace spn::threading {

/// Pacing strategy of spinning-then-yielding
class SpinPacing : public IPacingStrategy {
public:
    /// Return the shared spin pacing instance
    static SpinPacing& instance();

    void wait() override {}

    void after_iteration() override;
};

} // namespace spn::threading
