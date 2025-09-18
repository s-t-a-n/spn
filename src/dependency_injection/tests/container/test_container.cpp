#include "spn/dependency_injection/container.hpp"
#include "spn/dependency_injection/phases.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_container);

namespace {

using namespace spn::di;

/// test services
struct Logger {
    bool _initialized = false;
    void log(const char*) { _initialized = true; }
};

struct Database {
    bool _connected = false;
    void connect() { _connected = true; }
};

/// global instances for providers
Logger   logger_instance;
Database db_instance;

/// providers
static constexpr auto logger_ref = ref<[]() -> Logger& { return logger_instance; }>;
static constexpr auto db_ref     = ref<[]() -> Database& { return db_instance; }>;

/// service functions
void init_logger(Logger& l) {
    LOG_INF("initializing logger");
    l._initialized = true;
}

void init_db(Database& db, Logger& logger) {
    LOG_INF("initializing database");
    // note: don't call logger.log() to avoid side effects in phased tests
    db.connect();
}

void run_service(Logger& logger, Database& db) {
    LOG_INF("running service");
    logger.log("service started");
}

/// reset global state before each test
static void reset_globals(void* fixture) {
    logger_instance._initialized = false;
    db_instance._connected       = false;
}

} // namespace

ZTEST_SUITE(container_tests, NULL, NULL, reset_globals, NULL, NULL);

ZTEST(container_tests, test_empty_container) {
    auto container = injector();
    // Basic test that empty container can be created
    LOG_INF("Empty container created successfully");
}

ZTEST(container_tests, test_provider_injection) {
    auto container = injector() | inject(logger_ref) | inject(db_ref);

    /// verify we can invoke functions with dependencies
    container.invoke([](Logger& l) { zassert_false(l._initialized, "logger should not be initialized yet"); });

    container.invoke([](Database& db) { zassert_false(db._connected, "database should not be connected yet"); });
}

ZTEST(container_tests, test_call_recording_and_execution) {
    auto container =
        injector() | inject(logger_ref) | inject(db_ref) | call(&init_logger) | call(&init_db) | call(&run_service);

    /// nothing should be executed yet
    zassert_false(logger_instance._initialized, "logger should not be initialized before run");
    zassert_false(db_instance._connected, "database should not be connected before run");

    /// run all calls in default phase
    container.run();

    /// verify execution
    zassert_true(logger_instance._initialized, "logger should be initialized after run");
    zassert_true(db_instance._connected, "database should be connected after run");
}

ZTEST(container_tests, test_phased_execution) {
    auto container = injector() | inject(logger_ref) | inject(db_ref) | call_in<phase::init>(&init_logger)
                     | call_in<phase::configure>(&init_db) | call_in<phase::runtime>(&run_service);

    /// run phases in order
    container.run_phase<phase::configure>();
    zassert_false(logger_instance._initialized, "logger should not be initialized in configure phase");
    zassert_true(db_instance._connected, "database should be connected in configure phase");

    container.run_phase<phase::init>();
    zassert_true(logger_instance._initialized, "logger should be initialized in init phase");

    /// run multiple phases at once
    container.run_phases<phase::configure, phase::init, phase::runtime>();
}

ZTEST(container_tests, test_invoke_function) {
    auto container = injector() | inject(logger_ref) | inject(db_ref);

    /// test direct function invocation
    int result = container.invoke([](Logger&, Database&) -> int { return 42; });

    zassert_equal(result, 42, "invoke should return function result");
}