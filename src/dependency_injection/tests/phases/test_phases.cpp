#include "spn/dependency_injection/container.hpp"
#include "spn/dependency_injection/phases.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_phases);

namespace {

using namespace spn::di;

/// test system state
struct SystemState {
    bool backend_initialized = false;
    bool settings_registered = false;
    bool settings_loaded     = false;
    bool configured          = false;
    bool initialized         = false;
    bool running             = false;

    void reset() {
        backend_initialized = false;
        settings_registered = false;
        settings_loaded     = false;
        configured          = false;
        initialized         = false;
        running             = false;
    }
};

SystemState           system_state;
static constexpr auto state_ref = ref<[]() -> SystemState& { return system_state; }>;

/// phase functions
void init_backend(SystemState& state) {
    LOG_INF("backend init phase");
    state.backend_initialized = true;
}

void register_settings(SystemState& state) {
    LOG_INF("register settings phase");
    state.settings_registered = true;
}

void load_settings(SystemState& state) {
    LOG_INF("load settings phase");
    state.settings_loaded = state.settings_registered;
}

void configure_system(SystemState& state) {
    LOG_INF("configure phase");
    state.configured = state.backend_initialized && state.settings_loaded;
}

void initialize_system(SystemState& state) {
    LOG_INF("init phase");
    state.initialized = state.configured;
}

void run_system(SystemState& state) {
    LOG_INF("runtime phase");
    state.running = state.initialized;
}

} // namespace

ZTEST_SUITE(phases_tests, NULL, NULL, NULL, NULL, NULL);

ZTEST(phases_tests, test_single_phase_execution) {
    system_state.reset();

    auto container = injector() | inject(state_ref) | call_in<phase::configure>(&configure_system);

    /// initially nothing should be done
    zassert_false(system_state.configured, "system should not be configured initially");

    /// run only configure phase
    container.run_phase<phase::configure>();
    zassert_false(system_state.configured, "system should not be configured without dependencies");

    /// manually set dependencies and retry
    system_state.backend_initialized = true;
    system_state.settings_loaded     = true;
    container.run_phase<phase::configure>();
    zassert_true(system_state.configured, "system should be configured after dependencies met");
}

ZTEST(phases_tests, test_multi_phase_ordering) {
    system_state.reset();

    auto container = injector() | inject(state_ref) | call_in<phase::backend_init>(&init_backend)
                     | call_in<phase::register_settings>(&register_settings)
                     | call_in<phase::load_settings>(&load_settings) | call_in<phase::configure>(&configure_system)
                     | call_in<phase::init>(&initialize_system) | call_in<phase::runtime>(&run_system);

    /// run phases in correct order
    container.run_phases<
        phase::backend_init,
        phase::register_settings,
        phase::load_settings,
        phase::configure,
        phase::init,
        phase::runtime>();

    /// verify all phases completed successfully
    zassert_true(system_state.backend_initialized, "backend should be initialized");
    zassert_true(system_state.settings_registered, "settings should be registered");
    zassert_true(system_state.settings_loaded, "settings should be loaded");
    zassert_true(system_state.configured, "system should be configured");
    zassert_true(system_state.initialized, "system should be initialized");
    zassert_true(system_state.running, "system should be running");
}

ZTEST(phases_tests, test_wrong_phase_order) {
    system_state.reset();

    auto container = injector() | inject(state_ref) | call_in<phase::backend_init>(&init_backend)
                     | call_in<phase::register_settings>(&register_settings)
                     | call_in<phase::load_settings>(&load_settings) | call_in<phase::configure>(&configure_system)
                     | call_in<phase::init>(&initialize_system);

    /// run phases in wrong order (skip backend_init and register_settings)
    container.run_phases<phase::load_settings, phase::configure, phase::init>();

    /// verify cascade failure
    zassert_false(system_state.backend_initialized, "backend should not be initialized");
    zassert_false(system_state.settings_registered, "settings should not be registered");
    zassert_false(system_state.settings_loaded, "settings should not be loaded without registration");
    zassert_false(system_state.configured, "system should not be configured without backend");
    zassert_false(system_state.initialized, "system should not be initialized without configuration");
}

ZTEST(phases_tests, test_mixed_phase_and_default) {
    system_state.reset();

    auto container = injector() | inject(state_ref) | call_in<phase::backend_init>(&init_backend)
                     | call(&register_settings) // default phase
                     | call_in<phase::configure>(&configure_system);

    /// run backend init phase
    container.run_phase<phase::backend_init>();
    zassert_true(system_state.backend_initialized, "backend should be initialized");
    zassert_false(system_state.settings_registered, "settings should not be registered yet");

    /// run default phase
    container.run();
    zassert_true(system_state.settings_registered, "settings should be registered in default phase");

    /// manually prepare for configure phase
    system_state.settings_loaded = true;
    container.run_phase<phase::configure>();
    zassert_true(system_state.configured, "system should be configured");
}