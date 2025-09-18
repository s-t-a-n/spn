#include "spn/dependency_injection/container.hpp"
#include "spn/dependency_injection/phases.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spn_di_phased_init);

namespace {

using namespace spn::di;

/// logging service with configuration support
struct Logger {
    struct Config {
        int foo = 0;
    };
    void write(const char*) noexcept {};
};

namespace logger {

inline Logger         logger_inst;
inline Logger::Config logger_cfg_inst;

static constexpr auto logger_ref = ref<[]() -> Logger& { return logger_inst; }>;
static constexpr auto cfg_ref    = ref<[]() -> Logger::Config& { return logger_cfg_inst; }>;

/// configure logger with provided config
static void cfg_logger(Logger& l, Logger::Config& cfg) { LOG_INF("logger configured"); }
/// initialize logger service
static void init_logger(Logger& l) { LOG_INF("logger initialized"); }

/// module definition for phased logger initialization
inline constexpr auto mod =
    seq(inject(cfg_ref), inject(logger_ref), call_in<phase::configure>(&cfg_logger), call_in<phase::init>(&init_logger)
    );

} // namespace logger

/// uart communication service
struct Uart {
    struct Config {
        int foo = 0;
    };
    void send(const char*) noexcept { LOG_INF("hello from uart"); };
};

namespace uart {

inline Uart           uart_inst;
inline Uart::Config   uart_cfg_inst;
static constexpr auto uart_ref = ref<[]() -> Uart& { return uart_inst; }>;
static constexpr auto cfg_ref  = ref<[]() -> Uart::Config& { return uart_cfg_inst; }>;

/// configure uart with provided config
static void cfg_uart(Uart& l, Uart::Config& cfg) { LOG_INF("uart configured"); }
/// initialize uart service
static void init_uart(Uart&) { LOG_INF("uart initialized"); }

/// module definition for phased uart initialization
inline constexpr auto mod =
    seq(inject(cfg_ref), inject(uart_ref), call_in<phase::configure>(&cfg_uart), call_in<phase::init>(&init_uart));
} // namespace uart

/// sensor device with data reading capability
struct Sensor {
    struct Config {
        int foo = 0;
    };
    int read() noexcept {
        LOG_INF("hello from sensor");
        return 42;
    }
};
namespace sensor {

inline Sensor         sensor_inst;
inline Sensor::Config sensor_cfg_inst;
static constexpr auto sens_ref = ref<[]() -> Sensor& { return sensor_inst; }>;
static constexpr auto cfg_ref  = ref<[]() -> Sensor::Config& { return sensor_cfg_inst; }>;

/// configure sensor with provided config
static void cfg_sensor(Sensor& l, Sensor::Config& cfg, Logger::Config& logger_cfg) { LOG_INF("sensor configured"); }
/// initialize sensor service
static void init_sensor(Sensor&, Logger&) { LOG_INF("sensor initialized"); }

/// module definition for phased sensor initialization
inline constexpr auto mod =
    seq(inject(cfg_ref), inject(sens_ref), call_in<phase::configure>(&cfg_sensor), call_in<phase::init>(&init_sensor));
} // namespace sensor

/// execute application with injected dependencies
static void run(Uart& u, Sensor& s) { LOG_INF("run!"); }

/// main entry point for DI demonstration
int experimental_main_di() {
    using spn::di::operator|; // bring the pipe into ADL

    constexpr auto di = injector() | logger::mod | uart::mod | sensor::mod | call(&run)
                        | call([]() -> void { LOG_INF("hi from inline"); });

    di.run_phase<phase::configure>();
    di.run_phase<phase::init>();

    di.run();
    return 0;
}

} // namespace

int main() {
    LOG_INF("SPN Dependency Injection Phased Initialization Sample");
    return experimental_main_di();
}