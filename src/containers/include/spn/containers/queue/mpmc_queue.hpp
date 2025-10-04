#pragma once

#include "spn/containers/queue/mpmc_deque.hpp"

namespace spn {

/// Multi-producer multi-consumer bounded queue. (ISR unsafe)
template<typename T, size_t Capacity>
class MPMCQueue : public MPMCDeque<T, Capacity> {
    using Base = MPMCDeque<T, Capacity>;

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
