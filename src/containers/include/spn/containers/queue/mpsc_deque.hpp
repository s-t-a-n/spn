#pragma once

#include "spn/containers/concepts.hpp"
#include "spn/containers/queue/deque.hpp"
#include "spn/containers/queue/detail/policies.hpp"

namespace spn {

/// Multi-producer single-consumer bounded deque. (ISR unsafe)
template<typename T, size_t Capacity>
class MPSCDeque : public queue::Deque<T, Capacity, queue::detail::MPSCLockPolicy> {
    using Base = queue::Deque<T, Capacity, queue::detail::MPSCLockPolicy>;

public:
    using value_type          = typename Base::value_type;
    using single_consumer_tag = SingleConsumer;

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
