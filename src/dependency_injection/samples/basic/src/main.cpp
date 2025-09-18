#include "spn/dependency_injection/container.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(basic_sample);

namespace {

using namespace spn::di;

/// logger service
struct Logger {
    void info(const char* msg) { LOG_INF("Logger: %s", msg); }
};

/// database service
struct Database {
    bool _connected = false;

    void connect() {
        _connected = true;
        LOG_INF("Database connected");
    }

    void disconnect() {
        _connected = false;
        LOG_INF("Database disconnected");
    }

    bool is_connected() const { return _connected; }
};

/// application service that depends on logger and database
struct AppService {
    void start(Logger& logger, Database& db) {
        logger.info("Starting application service");

        if (!db.is_connected()) {
            db.connect();
        }

        logger.info("Application service started successfully");
    }

    void stop(Logger& logger, Database& db) {
        logger.info("Stopping application service");
        db.disconnect();
        logger.info("Application service stopped");
    }
};

/// global instances
Logger     logger_instance;
Database   db_instance;
AppService app_instance;

/// providers
static constexpr auto logger_ref = ref<[]() -> Logger& { return logger_instance; }>;
static constexpr auto db_ref     = ref<[]() -> Database& { return db_instance; }>;
static constexpr auto app_ref    = ref<[]() -> AppService& { return app_instance; }>;

/// wire dependencies and start application
void start_app(AppService& app, Logger& logger, Database& db) { app.start(logger, db); }
/// stop application and cleanup resources
void stop_app(AppService& app, Logger& logger, Database& db) { app.stop(logger, db); }

/// demonstrate basic dependency injection
void demo_basic_injection() {
    LOG_INF("=== Basic Dependency Injection Demo ===");

    /// create container with all dependencies
    auto container = injector() | inject(logger_ref) | inject(db_ref) | inject(app_ref) | call(&start_app);

    /// execute the injected calls
    container.run();

    /// demonstrate direct invocation
    container.invoke(&stop_app);

    LOG_INF("=== Demo Complete ===");
}

} // namespace

int main() {
    LOG_INF("SPN Dependency Injection Basic Sample");

    demo_basic_injection();

    return 0;
}