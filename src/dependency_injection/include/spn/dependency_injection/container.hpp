#pragma once

#include "spn/core/tuple_utils.hpp"
#include "spn/dependency_injection/providers.hpp"

#include <etl/tuple.h>
#include <etl/type_traits.h>
#include <etl/utility.h>

/*
 * Sources:
 * - Modern C++ Design by Andrei Alexandrescu
 * - "C++Now 2019: Kris Jusiak "Dependency Injection - a 25-dollar term for a 5-cent concept""
 *   https://www.youtube.com/watch?v=yVogS4NbL6U
 * - "How to Use C++ Dependency Injection to Write Maintainable Software - Francesco Zoffoli CppCon 2022"
 *   https://www.youtube.com/watch?v=l6Y9PqyK1Mc
 */

namespace spn::di {

namespace detail {

/// extract function argument types as tuple
template<typename>
struct fn_traits;

template<typename R, typename... A>
struct fn_traits<R (*)(A...)> {
    using args = etl::tuple<A...>;
};

template<typename C, typename R, typename... A>
struct fn_traits<R (C::*)(A...) const> {
    using args = etl::tuple<A...>;
};

template<typename C, typename R, typename... A>
struct fn_traits<R (C::*)(A...)> {
    using args = etl::tuple<A...>;
};

template<typename T>
struct fn_traits : fn_traits<decltype(&T::operator())> {};

/// find provider whose value_type matches Wanted
template<typename Wanted, typename Tuple, size_t I = 0>
static constexpr decltype(auto) tuple_find(Tuple& tup) {
    using prov_t = etl::tuple_element_t<I, Tuple>;
    if constexpr (etl::is_same_v<typename prov_t::value_type, Wanted>) return etl::get<I>(tup).get();
    else {
        static_assert(I + 1 < etl::tuple_size_v<Tuple>, "missing provider for requested type");
        return tuple_find<Wanted, Tuple, I + 1>(tup);
    }
}

/// check at compile time that Tuple already has provider for Arg
template<typename Arg, typename Tuple>
inline constexpr bool has_provider_v = []<size_t... I>(etl::index_sequence<I...>) {
    return (... || etl::is_same_v<Arg, typename etl::tuple_element_t<I, Tuple>::value_type>);
}(etl::make_index_sequence<etl::tuple_size_v<Tuple>>{});

} // namespace detail

/// compile-time dependency injection container
template<typename ProvTuple, typename CallTuple>
class container {
    ProvTuple _prov;
    CallTuple _calls;

    template<size_t I, typename F>
    using arg_t = etl::tuple_element_t<I, typename detail::fn_traits<F>::args>;

    template<typename F, size_t... I>
    static decltype(auto) call_impl(F f, etl::index_sequence<I...>, const ProvTuple& prov) {
        return f(detail::tuple_find<arg_t<I, F>>(prov)...);
    }

    template<typename Tag, typename F>
    static constexpr auto append(CallTuple const& t, F f) {
        auto rec = call_record<Tag, F>{f};
        return etl::tuple_cat(t, etl::tuple{rec});
    }

public:
    constexpr container() = default;
    constexpr container(ProvTuple p, CallTuple c) : _prov(p), _calls(c) {}

    /// add provider, error on duplicate
    template<Provider P>
    [[nodiscard]] constexpr auto inject(P p) const {
        constexpr bool duplicate = [&]<size_t... I>(etl::index_sequence<I...>) {
            return (... || etl::is_same_v<typename etl::tuple_element_t<I, ProvTuple>::value_type, typename P::value_type>);
        }(etl::make_index_sequence<etl::tuple_size_v<ProvTuple>>{});
        static_assert(!duplicate, "duplicate provider for this type");

        auto prov_next = etl::tuple_cat(_prov, etl::tuple{p});
        return container<decltype(prov_next), CallTuple>{prov_next, _calls};
    }

    /// add call, error if dependencies missing
    template<typename Tag = default_phase, typename F>
    [[nodiscard]] constexpr auto add_call(F f) const {
        constexpr bool ok = [&]<size_t... I>(etl::index_sequence<I...>) {
            return (... && detail::has_provider_v<arg_t<I, F>, ProvTuple>);
        }(etl::make_index_sequence<etl::tuple_size_v<typename detail::fn_traits<F>::args>>{});
        static_assert(ok, "call refers to a type without provider");

        auto next = append<Tag>(_calls, f);
        return container<ProvTuple, decltype(next)>{_prov, next};
    }

    /// invoke function with injected args
    template<typename F>
    decltype(auto) invoke(F f) const {
        constexpr size_t N = etl::tuple_size_v<typename detail::fn_traits<F>::args>;
        return call_impl(f, etl::make_index_sequence<N>{}, _prov);
    }

    /// run recorded calls for a single phase
    template<typename Tag>
    constexpr void invoke_calls_phase() const {
        spn::apply(
            [this]<typename... Recs>(Recs const&... r) {
                (..., ([&] {
                     if constexpr (etl::is_same_v<typename Recs::tag, Tag>) invoke(r.fn);
                 }()));
            },
            _calls
        );
    }

    /// run one phase
    template<typename Tag>
    constexpr void run_phase() const {
        invoke_calls_phase<Tag>();
    }

    /// run phases in the given order
    template<typename... Tags>
    constexpr void run_phases() const {
        (invoke_calls_phase<Tags>(), ...);
    }

    /// run default phase
    constexpr void run() const { run_phase<default_phase>(); }
};

template<typename P, typename C>
container(P, C) -> container<P, C>;

/// create an empty container
[[nodiscard]] constexpr auto injector() { return container<etl::tuple<>, etl::tuple<>>{}; }

} // namespace spn::di