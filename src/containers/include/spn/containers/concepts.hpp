#pragma once

#include <etl/utility.h>
#include <zephyr/kernel.h>

#include <concepts>
#include <cstddef>

namespace spn {

struct SingleConsumer {};
struct MultiConsumer {};

template<typename Q>
concept Queue = requires {
    typename Q::value_type;
} && requires(Q& queue, const typename Q::value_type& in, typename Q::value_type& out, k_timeout_t timeout) {
    { Q::capacity() } -> std::same_as<size_t>;
    { queue.size() } -> std::same_as<size_t>;
    { queue.empty() } -> std::convertible_to<bool>;
    { queue.full() } -> std::convertible_to<bool>;
    { queue.push(in, timeout) } -> std::same_as<bool>;
    { queue.push(etl::move(out), timeout) } -> std::same_as<bool>;
    { queue.pop(out, timeout) } -> std::same_as<bool>;
};

template<typename Q>
concept SingleConsumerQueue = Queue<Q> && requires { typename Q::single_consumer_tag; }
                              && std::same_as<typename Q::single_consumer_tag, SingleConsumer>;

template<typename Q>
concept MultiConsumerQueue = Queue<Q> && requires { typename Q::multi_consumer_tag; }
                             && std::same_as<typename Q::multi_consumer_tag, MultiConsumer>;

} // namespace spn
