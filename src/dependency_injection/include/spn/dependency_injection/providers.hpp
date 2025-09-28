#pragma once

#include "spn/core/type_traits.hpp"

#include <etl/tuple.h>
#include <etl/type_traits.h>
#include <etl/utility.h>

namespace spn::di {

/// function pointer type concept
template<typename T>
concept FunctionPtr = etl::is_pointer_v<T> && spn::is_function_v<etl::remove_pointer_t<T>>;

/// provider concept for dependency injection
template<typename P>
concept Provider = requires { typename P::value_type; } && etl::is_lvalue_reference_v<typename P::value_type>;

/// pipeable operation concept
template<typename C, typename F>
concept Pipeable = requires(C c, F f) { f(etl::move(c)); };

/// wrap free function returning l-value into provider
template<auto Fn>
struct ref_t {
    using value_type = decltype(Fn());
    static_assert(etl::is_lvalue_reference_v<value_type>, "provider must return l-value reference");
    static constexpr value_type get() { return Fn(); }
};

/// convenience variable template for ref_t
template<auto Fn>
inline constexpr ref_t<Fn> ref{};

/// pipe operator for chaining operations
template<typename C, typename F>
    requires Pipeable<C, F>
constexpr auto operator|(C&& c, F&& f) {
    return etl::forward<F>(f)(etl::forward<C>(c));
}

/// default phase tag for unspecified execution phase
struct default_phase {};

/// wrapper storing phase tag with callable
template<typename Tag, typename F>
struct call_record {
    using tag = Tag;
    F fn;
};

/// create call adapter for specific phase
template<typename Tag = default_phase, typename F>
constexpr auto call_in(F f) {
    return [f]<typename D>(D d) { return d.template add_call<Tag>(f); };
}

/// create call adapter from function pointer
template<FunctionPtr Fn>
constexpr auto call(Fn fn) {
    return call_in(fn);
}

/// create call adapter from stateless functor
template<typename F>
    requires spn::is_empty_v<F> && etl::is_trivially_copyable_v<F> && (!FunctionPtr<F>)
constexpr auto call(F f) {
    return call_in(f);
}

/// adapter for piping in a provider
template<Provider P>
[[nodiscard]] constexpr auto inject(P p) {
    return [p](auto d) { return d.inject(p); };
}

/// compose several adaptors into one, preserving order
template<typename... Adaptors>
[[nodiscard]] constexpr auto seq(Adaptors... a) {
    return [=]<typename D>(D d) { return (d | ... | a); };
}

} // namespace spn::di