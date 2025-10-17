#include "spn/core/type_traits.hpp"
#include "spn/dependency_injection/container.hpp"
#include "spn/dependency_injection/providers.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_providers);

namespace {

using namespace spn::di;

// test data types
struct ConfigA {
    int value = 42;
};
struct ConfigB {
    int value = 24;
};
struct Service {
    bool active = false;
};

// global instances
ConfigA config_a_instance;
ConfigB config_b_instance;
Service service_instance;

// providers
static constexpr auto config_a_ref = ref<[]() -> ConfigA& { return config_a_instance; }>;
static constexpr auto config_b_ref = ref<[]() -> ConfigB& { return config_b_instance; }>;
static constexpr auto service_ref  = ref<[]() -> Service& { return service_instance; }>;

// test functions
int test_func_ptr(ConfigA& a) { return a.value; }

void activate_service(Service& s, ConfigA& a, ConfigB& b) { s.active = (a.value + b.value) > 50; }

struct TestFunctor {
    int operator()(ConfigA& a) const { return a.value * 2; }
};

} // namespace

ZTEST_SUITE(providers_tests, NULL, NULL, NULL, NULL, NULL);

ZTEST(providers_tests, test_ref_provider_types) {
    // verify provider types
    static_assert(etl::is_same_v<typename decltype(config_a_ref)::value_type, ConfigA&>);
    static_assert(etl::is_same_v<typename decltype(config_b_ref)::value_type, ConfigB&>);
    static_assert(etl::is_same_v<typename decltype(service_ref)::value_type, Service&>);
}

ZTEST(providers_tests, test_provider_concept) {
    // verify provider concept satisfaction
    static_assert(Provider<decltype(config_a_ref)>);
    static_assert(Provider<decltype(config_b_ref)>);
    static_assert(Provider<decltype(service_ref)>);
}

ZTEST(providers_tests, test_function_pointer_call) {
    auto container = injector() | inject(config_a_ref) | call(&test_func_ptr);

    // verify function pointer handling
    static_assert(FunctionPtr<decltype(&test_func_ptr)>);

    // test execution
    int result = 0;
    container.invoke([&result](ConfigA& a) { result = test_func_ptr(a); });
    zassert_equal(result, 42, "function pointer should be callable");
}

ZTEST(providers_tests, test_functor_call) {
    auto container = injector() | inject(config_a_ref) | call(TestFunctor{});

    // verify functor concept requirements
    static_assert(spn::is_empty_v<TestFunctor>);
    static_assert(etl::is_trivially_copyable_v<TestFunctor>);
    static_assert(!FunctionPtr<TestFunctor>);

    // test execution via invoke
    int result = container.invoke(TestFunctor{});
    zassert_equal(result, 84, "functor should be callable and return doubled value");
}

ZTEST(providers_tests, test_multi_dependency_injection) {
    service_instance.active = false;
    config_a_instance.value = 30;
    config_b_instance.value = 25;

    auto container =
        injector() | inject(config_a_ref) | inject(config_b_ref) | inject(service_ref) | call(&activate_service);

    // verify service is not active initially
    zassert_false(service_instance.active, "service should not be active initially");

    // run the activation
    container.run();

    // verify service activation based on config values
    zassert_true(service_instance.active, "service should be active after injection");
}

ZTEST(providers_tests, test_seq_composition) {
    auto module_a = seq(inject(config_a_ref), call([](ConfigA& a) { a.value = 100; }));

    auto module_b = seq(inject(config_b_ref), inject(service_ref), call(&activate_service));

    auto container = injector() | module_a | module_b;

    container.run();

    // service should be active (100 + 24 > 50)
    zassert_true(service_instance.active, "service should be active after seq composition");
}