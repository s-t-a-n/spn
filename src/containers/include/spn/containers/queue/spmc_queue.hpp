#pragma once

#include "spn/containers/queue/spmc_deque.hpp"

namespace spn {

/// Single-producer multi-consumer bounded queue. (ISR unsafe)
template<typename T, size_t Capacity>
class SPMCQueue : public SPMCDeque<T, Capacity> {
    using Base = SPMCDeque<T, Capacity>;

public:
    using value_type         = Base::value_type;
    using multi_consumer_tag = MultiConsumer;

    using Base::Base;
    using Base::capacity;
    using Base::clear;
    using Base::emplace;
    using Base::empty;
    using Base::full;
    using Base::pop;
    using Base::push;
    using Base::size;
};

} // namespace spn
