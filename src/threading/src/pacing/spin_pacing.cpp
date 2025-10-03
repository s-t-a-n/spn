#include "spn/threading/pacing/spin_pacing.hpp"

namespace spn::threading {

SpinPacing& SpinPacing::instance() {
    static SpinPacing pacing{};
    return pacing;
}

void SpinPacing::after_iteration() { k_yield(); }

} // namespace spn::threading
