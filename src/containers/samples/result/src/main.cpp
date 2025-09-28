#include <spn/containers/result.hpp>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(result_sample, LOG_LEVEL_INF);

enum class ErrorCode : uint8_t { None = 0, SomethingWentWrong, ValueTooSmall, InvalidIntermediary };

const char* error_code_to_string(ErrorCode code) {
    switch (code) {
    case ErrorCode::None: return "None";
    case ErrorCode::SomethingWentWrong: return "Something went wrong";
    case ErrorCode::ValueTooSmall: return "Value too small";
    case ErrorCode::InvalidIntermediary: return "Invalid intermediary value";
    default: return "Unknown error";
    }
}

void basic_usage() {
    using namespace spn;

    Result<int, ErrorCode> success_result(10);
    Result<int, ErrorCode> error_result = Result<int, ErrorCode>::failed(ErrorCode::SomethingWentWrong);

    if (success_result.is_ok()) LOG_INF("Success result value: %d", success_result.value());
    if (error_result.is_err()) LOG_INF("Error result: %s", error_code_to_string(error_result.error()));
}

void transformation_operations() {
    using namespace spn;

    Result<int, ErrorCode> success_result(10);
    Result<int, ErrorCode> error_result = Result<int, ErrorCode>::failed(ErrorCode::SomethingWentWrong);

    // map transforms success value
    auto mapped_result = success_result.map([](const int& value) { return value * 2; });
    if (mapped_result.is_ok()) LOG_INF("Mapped success value: %d", mapped_result.value());

    // map_error transforms error value
    auto mapped_error_result = error_result.map_error([](ErrorCode err) { return ErrorCode::SomethingWentWrong; });
    if (mapped_error_result.is_err())
        LOG_INF("Mapped error result: %s", error_code_to_string(mapped_error_result.error()));
}

void error_recovery() {
    using namespace spn;

    Result<int, ErrorCode> success_result(10);
    Result<int, ErrorCode> error_result = Result<int, ErrorCode>::failed(ErrorCode::SomethingWentWrong);

    // and_then chains operations on success
    auto chained_result = success_result.and_then([](const int& value) -> Result<int, ErrorCode> {
        if (value > 5) return Result<int, ErrorCode>(value * 3);
        return Result<int, ErrorCode>::failed(ErrorCode::ValueTooSmall);
    });
    if (chained_result.is_ok()) LOG_INF("Chained result success: %d", chained_result.value());

    // or_else provides fallback on error
    auto recovered_result = error_result.or_else([](ErrorCode err) -> Result<int, ErrorCode> {
        LOG_INF("Recovering from error: %s", error_code_to_string(err));
        return Result<int, ErrorCode>(42);
    });
    if (recovered_result.is_ok()) LOG_INF("Recovered result success: %d", recovered_result.value());
}

void intermediary_processing() {
    using namespace spn;

    Result<int, ErrorCode, int> intermediary_result = Result<int, ErrorCode, int>::intermediary(5);

    if (intermediary_result.is_intermediary())
        LOG_INF("Intermediary result value: %d", intermediary_result.intermediary_value());

    // chain processes intermediary states
    auto final_result = intermediary_result
                            .chain([](int intermediary_value) {
                                if (intermediary_value > 0) return Result<int, ErrorCode, int>(intermediary_value + 10);
                                return Result<int, ErrorCode, int>::failed(ErrorCode::InvalidIntermediary);
                            })
                            .chain([](int intermediary_value) {
                                if (intermediary_value > 0) return Result<int, ErrorCode, int>(intermediary_value + 10);
                                return Result<int, ErrorCode, int>::failed(ErrorCode::InvalidIntermediary);
                            });
    if (final_result.is_ok()) LOG_INF("Final result success: %d", final_result.value());
}

int main() {
    basic_usage();
    transformation_operations();
    error_recovery();
    intermediary_processing();
    return 0;
}