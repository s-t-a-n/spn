#include "spn/containers/result.hpp"

#include <etl/string.h>
#include <zephyr/ztest.h>

#include <climits>
#include <cstdint>
#include <cstdlib>

using namespace spn;

ZTEST_SUITE(result_suite, NULL, NULL, NULL, NULL, NULL);

namespace {
using IntResult = Result<int, etl::string<64>, int>;

IntResult make_success(int value) { return IntResult(value); }
IntResult make_failure(const etl::string<64>& message) { return IntResult::failed(message); }
IntResult make_failure(const char* message) { return IntResult::failed(etl::string<64>(message)); }
IntResult make_intermediary(int value) { return IntResult::intermediary(value); }
} // namespace

ZTEST(result_suite, test_pipeline_with_chain) {
    using ParseResult = Result<float, etl::string<64>, int>;

    const auto stage_entry  = [](int input) { return ParseResult::intermediary(input); };
    const auto stage_double = [](int value) -> ParseResult {
        if (value == 0) return ParseResult::failed("value was 0");
        return ParseResult(static_cast<float>(value * 2));
    };
    const auto stage_error     = [](int) { return ParseResult::failed("stage_error"); };
    const auto stage_increment = [](int value) { return ParseResult::intermediary(value + 1); };

    const auto success = stage_entry(5).chain(stage_double);
    zassert_true(success.is_ok());
    zassert_equal(10, success.value());

    const auto failure = stage_entry(5).chain(stage_error).chain(stage_double);
    zassert_true(failure.is_err());
    zassert_str_equal("stage_error", failure.error().c_str());

    const auto zero_fail = stage_entry(0).chain(stage_double);
    zassert_true(zero_fail.is_err());
    zassert_str_equal("value was 0", zero_fail.error().c_str());

    const auto multi_intermediate = stage_entry(5).chain(stage_increment).chain(stage_increment).chain(stage_double);
    zassert_true(multi_intermediate.is_ok());
    zassert_equal(14, multi_intermediate.value());

    const auto early_success = ParseResult(42.0f).chain(stage_error).chain(stage_double);
    zassert_true(early_success.is_ok());
    zassert_equal(42, early_success.value());
}

ZTEST(result_suite, test_construction_and_copy) {
    using ResultType = IntResult;

    ResultType success_source = make_success(42);
    ResultType success_copy(success_source);
    zassert_true(success_copy.is_ok());
    zassert_equal(42, success_copy.value());

    ResultType error_source = make_failure("test error");
    ResultType error_copy(error_source);
    zassert_true(error_copy.is_err());
    zassert_str_equal("test error", error_copy.error().c_str());

    ResultType assigned;
    assigned = success_source;
    zassert_true(assigned.is_ok());
    zassert_equal(42, assigned.value());

    assigned = assigned;
    zassert_true(assigned.is_ok());
    zassert_equal(42, assigned.value());

    ResultType moved_target;
    moved_target = etl::move(assigned);
    zassert_true(moved_target.is_ok());
    zassert_equal(42, moved_target.value());

    ResultType tagged_ok(ResultType::ok, 100);
    zassert_true(tagged_ok.is_ok());
    zassert_equal(100, tagged_ok.value());

    ResultType tagged_err(ResultType::err, etl::string<64>("tagged error"));
    zassert_true(tagged_err.is_err());
    zassert_str_equal("tagged error", tagged_err.error().c_str());

    ResultType tagged_mid(ResultType::mid, 250);
    zassert_true(tagged_mid.is_intermediary());
    zassert_equal(250, tagged_mid.intermediary_value());
}

ZTEST(result_suite, test_accessors_and_unwrap) {
    using ResultType = IntResult;

    ResultType success = make_success(42);
    zassert_equal(42, success.value());
    zassert_true(success.is_ok());

    auto failure = make_failure("error message");
    zassert_str_equal("error message", failure.error().c_str());
    zassert_true(failure.is_err());

    auto intermediary = make_intermediary(123);
    zassert_equal(123, intermediary.intermediary_value());
    zassert_true(intermediary.is_intermediary());

    ResultType no_value;
    zassert_false(no_value.is_ok() || no_value.is_err() || no_value.is_intermediary());

    int unwrapped_success = success.unwrap();
    zassert_equal(42, unwrapped_success);
    zassert_false(success.is_ok());

    etl::string<64> unwrapped_error = failure.unwrap_error();
    zassert_str_equal("error message", unwrapped_error.c_str());
    zassert_false(failure.is_err());

    int unwrapped_intermediary = intermediary.unwrap_intermediary_value();
    zassert_equal(123, unwrapped_intermediary);
    zassert_false(intermediary.is_intermediary());

    ResultType success_for_fallback = make_success(99);
    zassert_equal(99, success_for_fallback.unwrap_or_else([]() { return 0; }));

    ResultType error_for_fallback = make_failure("error");
    zassert_equal(500, error_for_fallback.unwrap_or_else([]() { return 500; }));

    ResultType intermediary_for_fallback = make_intermediary(200);
    zassert_equal(600, intermediary_for_fallback.unwrap_or_else([]() { return 600; }));

    ResultType no_value_for_fallback;
    zassert_equal(700, no_value_for_fallback.unwrap_or_else([]() { return 700; }));
}

ZTEST(result_suite, test_transformations) {
    using ResultType = IntResult;

    ResultType success      = make_success(10);
    ResultType failure      = make_failure("error");
    ResultType intermediary = make_intermediary(15);
    ResultType no_value;

    auto mapped_success = success.map([](const int& val) { return val * 2; });
    zassert_true(mapped_success.is_ok());
    zassert_equal(20, mapped_success.value());

    auto mapped_failure = failure.map([](const int& val) { return val * 2; });
    zassert_true(mapped_failure.is_err());
    zassert_str_equal("error", mapped_failure.error().c_str());

    auto mapped_intermediary = intermediary.map([](const int& val) { return val * 2; });
    zassert_true(mapped_intermediary.is_intermediary());
    zassert_equal(15, mapped_intermediary.intermediary_value());

    auto map_error_failure = failure.map_error([](const etl::string<64>& err) {
        auto result = etl::string<64>(err);
        result.append(" mapped");
        return result;
    });
    zassert_true(map_error_failure.is_err());
    zassert_str_equal("error mapped", map_error_failure.error().c_str());

    auto map_error_success = success.map_error([](const etl::string<64>& err) {
        auto result = etl::string<64>(err);
        result.append(" mapped");
        return result;
    });
    zassert_true(map_error_success.is_ok());
    zassert_equal(10, map_error_success.value());

    auto map_intermediate = intermediary.map_intermediary([](const int& inter) { return inter + 100; });
    zassert_true(map_intermediate.is_intermediary());
    zassert_equal(115, map_intermediate.intermediary_value());

    auto map_inter_success = success.map_intermediary([](const int& inter) { return inter + 100; });
    zassert_true(map_inter_success.is_ok());
    zassert_equal(10, map_inter_success.value());

    auto map_no_value = no_value.map([](const int& val) { return val * 2; });
    zassert_false(map_no_value.is_ok() || map_no_value.is_err() || map_no_value.is_intermediary());

    auto map_error_no_value = no_value.map_error([](const etl::string<64>& err) {
        auto result = etl::string<64>(err);
        result.append(" mapped");
        return result;
    });
    zassert_false(map_error_no_value.is_ok() || map_error_no_value.is_err() || map_error_no_value.is_intermediary());

    auto map_inter_no_value = no_value.map_intermediary([](const int& inter) { return inter + 100; });
    zassert_false(map_inter_no_value.is_ok() || map_inter_no_value.is_err() || map_inter_no_value.is_intermediary());

    auto and_then_success = success.and_then([](const int& val) { return make_success(val * 2); });
    zassert_true(and_then_success.is_ok());
    zassert_equal(20, and_then_success.value());

    auto and_then_to_error = success.and_then([](const int&) { return make_failure("value too large"); });
    zassert_true(and_then_to_error.is_err());
    zassert_str_equal("value too large", and_then_to_error.error().c_str());

    auto and_then_to_intermediate = success.and_then([](const int& val) { return make_intermediary(val + 100); });
    zassert_true(and_then_to_intermediate.is_intermediary());
    zassert_equal(110, and_then_to_intermediate.intermediary_value());

    auto and_then_failure = failure.and_then([](const int& val) { return make_success(val * 2); });
    zassert_true(and_then_failure.is_err());
    zassert_str_equal("error", and_then_failure.error().c_str());

    auto and_then_intermediary = intermediary.and_then([](const int& val) { return make_success(val * 2); });
    zassert_true(and_then_intermediary.is_intermediary());
    zassert_equal(15, and_then_intermediary.intermediary_value());

    auto and_then_no_value = no_value.and_then([](const int& val) { return make_success(val * 2); });
    zassert_false(and_then_no_value.is_ok() || and_then_no_value.is_err() || and_then_no_value.is_intermediary());

    auto or_else_failure = failure.or_else([](const etl::string<64>& err) { return make_success(42); });
    zassert_true(or_else_failure.is_ok());
    zassert_equal(42, or_else_failure.value());

    auto or_else_failure_fail = failure.or_else([](const etl::string<64>& err) {
        auto msg = etl::string<64>("recovery failed: ");
        msg.append(err);
        return make_failure(msg);
    });
    zassert_true(or_else_failure_fail.is_err());
    zassert_str_equal("recovery failed: error", or_else_failure_fail.error().c_str());

    auto or_else_success = success.or_else([](const etl::string<64>&) { return make_success(999); });
    zassert_true(or_else_success.is_ok());
    zassert_equal(10, or_else_success.value());

    auto or_else_intermediate = intermediary.or_else([](const etl::string<64>&) { return make_success(999); });
    zassert_true(or_else_intermediate.is_intermediary());
    zassert_equal(15, or_else_intermediate.intermediary_value());

    auto or_else_no_value = no_value.or_else([](const etl::string<64>&) { return make_success(999); });
    zassert_false(or_else_no_value.is_ok() || or_else_no_value.is_err() || or_else_no_value.is_intermediary());

    zassert_equal(42, make_success(42).value_or(99));
    zassert_equal(99, make_failure("error").value_or(99));
    zassert_equal(99, make_intermediary(123).value_or(99));
    zassert_equal(99, ResultType{}.value_or(99));

    zassert_str_equal("error", make_failure("error").error_or("default").c_str());
    zassert_str_equal("default", make_success(42).error_or("default").c_str());
    zassert_str_equal("default", make_intermediary(123).error_or("default").c_str());
    zassert_str_equal("default", ResultType{}.error_or("default").c_str());

    ResultType success_for_literal = make_success(42);
    auto       failure_for_literal = make_failure("error");

    auto success_value_or_double = success_for_literal.value_or(3.14);
    zassert_equal(42, success_value_or_double);

    auto failed_value_or_double = failure_for_literal.value_or(3.14);
    zassert_equal(3, failed_value_or_double);

    auto failed_error_or_literal = failure_for_literal.error_or("literal");
    zassert_str_equal("error", failed_error_or_literal.c_str());

    auto success_error_or_literal = success_for_literal.error_or("literal");
    zassert_str_equal("literal", success_error_or_literal.c_str());
}

ZTEST(result_suite, test_match_and_utilities) {
    using ResultType = IntResult;

    ResultType success      = make_success(42);
    ResultType failure      = make_failure("test error");
    ResultType intermediary = make_intermediary(15);

    auto success_match = success.match(
        [](const int& val) { return val * 2; },
        [](const etl::string<64>&) { return -1; },
        [](const int&) { return -2; }
    );
    zassert_equal(84, success_match);

    auto failure_match = failure.match(
        [](const int& val) { return val * 2; },
        [](const etl::string<64>& err) { return static_cast<int>(err.length()); },
        [](const int&) { return -2; }
    );
    zassert_equal(10, failure_match);

    auto intermediary_match = intermediary.match(
        [](const int& val) { return val * 2; },
        [](const etl::string<64>&) { return -1; },
        [](const int& inter) { return inter + 100; }
    );
    zassert_equal(115, intermediary_match);

    struct MatchResult {
        enum Type { SUCCESS, FAILED, INTERMEDIARY } type;
        int value;
    };

    auto success_type = success.match(
        [](const int& val) { return MatchResult{MatchResult::SUCCESS, val}; },
        [](const etl::string<64>&) { return MatchResult{MatchResult::FAILED, 0}; },
        [](const int& inter) { return MatchResult{MatchResult::INTERMEDIARY, inter}; }
    );
    zassert_equal(MatchResult::SUCCESS, success_type.type);
    zassert_equal(42, success_type.value);

    auto failure_type = failure.match(
        [](const int& val) { return MatchResult{MatchResult::SUCCESS, val}; },
        [](const etl::string<64>& err) { return MatchResult{MatchResult::FAILED, static_cast<int>(err.length())}; },
        [](const int& inter) { return MatchResult{MatchResult::INTERMEDIARY, inter}; }
    );
    zassert_equal(MatchResult::FAILED, failure_type.type);
    zassert_equal(10, failure_type.value);

    auto intermediary_type = intermediary.match(
        [](const int& val) { return MatchResult{MatchResult::SUCCESS, val}; },
        [](const etl::string<64>&) { return MatchResult{MatchResult::FAILED, 0}; },
        [](const int& inter) { return MatchResult{MatchResult::INTERMEDIARY, inter}; }
    );
    zassert_equal(MatchResult::INTERMEDIARY, intermediary_type.type);
    zassert_equal(15, intermediary_type.value);
}

ZTEST(result_suite, test_operator_arrow) {
    struct TestStruct {
        int             value;
        etl::string<64> name;

        int                    get_value() const { return value; }
        const etl::string<64>& get_name() const { return name; }
    };

    using ResultType = Result<TestStruct, etl::string<64>, TestStruct>;

    TestStruct test_obj{42, "test"};
    ResultType success_result(test_obj);

    zassert_equal(42, success_result->value);
    zassert_str_equal("test", success_result->name.c_str());

    zassert_equal(42, success_result->get_value());
    zassert_str_equal("test", success_result->get_name().c_str());

    TestStruct dereferenced = *success_result;
    zassert_equal(42, dereferenced.value);
    zassert_str_equal("test", dereferenced.name.c_str());

    const ResultType const_success_result(test_obj);
    zassert_equal(42, const_success_result->value);
    zassert_str_equal("test", const_success_result->name.c_str());

    const TestStruct const_dereferenced = *const_success_result;
    zassert_equal(42, const_dereferenced.value);
    zassert_str_equal("test", const_dereferenced.name.c_str());

    TestStruct modified_obj{99, "modified"};
    ResultType modified_result(modified_obj);
    zassert_equal(99, modified_result->value);
    zassert_str_equal("modified", modified_result->name.c_str());
    zassert_equal(99, modified_result->get_value());
    zassert_str_equal("modified", modified_result->get_name().c_str());
}

ZTEST(result_suite, test_void_result) {
    using VoidResult = Result<void, int, int>;

    VoidResult success{VoidResult::ok};
    zassert_true(success.is_ok());
    success.value();

    auto to_error = success.and_then([]() -> VoidResult { return VoidResult::failed(1); });
    zassert_true(to_error.is_err());
    zassert_equal(1, to_error.error());

    auto failure      = VoidResult::failed(2);
    auto recovery_err = failure.or_else([](int err) { return VoidResult::failed(err + 1); });
    zassert_true(recovery_err.is_err());
    zassert_equal(3, recovery_err.error());

    auto passthrough = success.or_else([](int) { return VoidResult::failed(99); });
    zassert_true(passthrough.is_ok());

    auto mapped_error = failure.map_error([](int err) { return err + 5; });
    zassert_true(mapped_error.is_err());
    zassert_equal(7, mapped_error.error());

    auto intermediary       = VoidResult::intermediary(9);
    auto intermediary_chain = intermediary.and_then([]() -> VoidResult { return VoidResult{VoidResult::ok}; });
    zassert_true(intermediary_chain.is_intermediary());
    zassert_equal(9, intermediary_chain.intermediary_value());

    auto intermediary_unwrap = intermediary;
    zassert_equal(9, intermediary_unwrap.unwrap_intermediary_value());

    auto matched_value = success.match([]() { return 11; }, [](int) { return 0; }, [](int) { return -1; });
    zassert_equal(11, matched_value);
}

ZTEST(result_suite, test_edge_cases) {
    using ResultType = IntResult;

    ResultType no_value;
    zassert_false(no_value.is_ok() || no_value.is_err() || no_value.is_intermediary());

    auto mapped_no_value = no_value.map([](const int& val) { return val * 2; });
    zassert_false(mapped_no_value.is_ok() || mapped_no_value.is_err() || mapped_no_value.is_intermediary());

    auto and_then_no_value = no_value.and_then([](const int& val) { return make_success(val * 2); });
    zassert_false(and_then_no_value.is_ok() || and_then_no_value.is_err() || and_then_no_value.is_intermediary());

    auto or_else_no_value = no_value.or_else([](const etl::string<64>&) { return make_success(999); });
    zassert_false(or_else_no_value.is_ok() || or_else_no_value.is_err() || or_else_no_value.is_intermediary());

    auto no_value_fallback = no_value.value_or(123);
    zassert_equal(123, no_value_fallback);

    auto no_value_error_fallback = no_value.error_or("default");
    zassert_str_equal("default", no_value_error_fallback.c_str());

    struct MoveOnly {
        int value;
        MoveOnly(int v) : value(v) {}
        MoveOnly(const MoveOnly&)            = delete;
        MoveOnly& operator=(const MoveOnly&) = delete;
        MoveOnly(MoveOnly&&)                 = default;
        MoveOnly& operator=(MoveOnly&&)      = default;
    };

    using MoveResult = Result<MoveOnly, int, int>;
    MoveResult move_success(MoveOnly{42});
    zassert_true(move_success.is_ok());
    zassert_equal(42, move_success.value().value);

    MoveOnly unwrapped = move_success.unwrap();
    zassert_equal(42, unwrapped.value);
}
