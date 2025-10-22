#pragma once

#include "spn/containers/queue/mpsc_deque.hpp"

namespace spn {

/// Multi-producer single-consumer bounded queue. (ISR unsafe)
template<typename T, size_t Capacity>
class MPSCQueue : public MPSCDeque<T, Capacity> {
    using Base = MPSCDeque<T, Capacity>;

public:
    using value_type          = typename Base::value_type;
    using single_consumer_tag = SingleConsumer;

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
