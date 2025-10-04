#pragma once

#include "spn/containers/concepts.hpp"
#include "spn/containers/queue/deque.hpp"
#include "spn/containers/queue/detail/policies.hpp"

namespace spn {

/// Multi-producer multi-consumer bounded deque. (ISR unsafe)
template<typename T, size_t Capacity>
class MPMCDeque : public queue::Deque<T, Capacity, queue::detail::MPMCLockPolicy> {
    using Base = queue::Deque<T, Capacity, queue::detail::MPMCLockPolicy>;

public:
    using value_type         = Base::value_type;
    using multi_consumer_tag = MultiConsumer;

    using Base::Base;
    using Base::capacity;
    using Base::clear;
    using Base::emplace;
    using Base::emplace_back;
    using Base::emplace_front;
    using Base::empty;
    using Base::full;
    using Base::pop;
    using Base::pop_back;
    using Base::pop_front;
    using Base::push;
    using Base::push_back;
    using Base::push_front;
    using Base::size;
};

} // namespace spn
