#include "spn/containers/result.hpp"

#include <zephyr/ztest.h>

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

using namespace spn;

ZTEST_SUITE(result_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(result_suite, test_result_basics) {
    using ParserResult     = Result<float>;
    const auto stage_entry = [](float input) -> ParserResult { return ParserResult(input); };
    const auto stage_count = [](ParserResult&& p) -> ParserResult {
        if (p.is_failed()) return p;
        return ParserResult{p.value() + 1};
    };
    const auto stage_fail = [](ParserResult&& p) -> ParserResult { return ParserResult::failed({}); };

    auto simple_result = stage_count(stage_entry(1));
    zassert_equal(true, bool(simple_result));
    zassert_equal(2.0f, *simple_result);

    auto failed_result = stage_count(stage_fail(stage_entry(1)));
    zassert_equal(false, bool(failed_result));
    zassert_equal(true, failed_result.is_failed());
}

ZTEST(result_suite, test_result_basic_parser) {
    class Parser {
    public:
        using ParserResult = Result<float, std::string>;

        Parser() {}

        ParserResult parse(float input) {
            _result = stage_entry(input);
            _result = stage_double();
            return _result;
        }
        ParserResult parse_with_error(float input) {
            _result = stage_entry(input);
            _result = stage_error();
            _result = stage_double();
            return _result;
        }

    protected:
        ParserResult stage_entry(float input) { return {input}; }
        ParserResult stage_double() {
            if (_result.is_failed()) return _result;

            if (_result.value() == 0) return ParserResult::failed("stage_double: value was 0");
            return {_result.value() * 2};
        }
        ParserResult stage_error() {
            if (_result.is_failed()) return _result;
            return ParserResult::failed("stage_error");
        }

    private:
        ParserResult _result;
    };

    Parser     p;
    const auto result = p.parse(5);
    zassert_equal(true, result.is_success());
    zassert_equal(false, result.is_failed());
    zassert_equal(10, result.value());

    const auto result_error = p.parse_with_error(5);
    zassert_equal(false, result_error.is_success());
    zassert_equal(true, result_error.is_failed());
    zassert_str_equal("stage_error", result_error.error_value().c_str());

    const auto result_conditional_error = p.parse(0);
    zassert_equal(false, result_conditional_error.is_success());
    zassert_equal(true, result_conditional_error.is_failed());
    zassert_str_equal("stage_double: value was 0", result_conditional_error.error_value().c_str());
}

ZTEST(result_suite, test_result_chain) {
    class Parser {
    public:
        using ParseResult = spn::Result<float, std::string, int>;

        Parser() {}

        ParseResult parse(float input) {
            return stage_entry(input).chain([this](float value) { return stage_double(value); });
        }

        ParseResult parse_with_error(float input) {
            return stage_entry(input).chain([this](float value) { return stage_error(value); }
            ).chain([this](float value) { return stage_double(value); });
        }

        ParseResult parse_with_early_success(float input) {
            return stage_entry(input).chain([this](float value) { return stage_early_success(value); }
            ).chain([this](float value) {
                 return stage_error(value);
             }).chain([this](float value) { return stage_double(value); });
        }

    protected:
        ParseResult stage_entry(float input) { return ParseResult::intermediary(input); }

        ParseResult stage_double(float value) {
            if (value == 0) {
                return ParseResult::failed("stage_double: value was 0");
            }
            return ParseResult(value * 2);
        }

        ParseResult stage_error(float value) { return ParseResult::failed("stage_error"); }
        ParseResult stage_early_success(float value) { return ParseResult(42); }
    };

    Parser     p;
    const auto result = p.parse(5);
    zassert_equal(true, result.is_success());
    zassert_equal(false, result.is_intermediary());
    zassert_equal(false, result.is_failed());
    zassert_equal(10, result.value());

    const auto result_error = p.parse_with_error(5);
    zassert_equal(false, result_error.is_success());
    zassert_equal(false, result_error.is_intermediary());
    zassert_equal(true, result_error.is_failed());
    zassert_str_equal("stage_error", result_error.error_value().c_str());

    const auto result_conditional_error = p.parse(0);
    zassert_equal(false, result_conditional_error.is_success());
    zassert_equal(false, result_conditional_error.is_intermediary());
    zassert_equal(true, result_conditional_error.is_failed());
    zassert_str_equal("stage_double: value was 0", result_conditional_error.error_value().c_str());

    const auto result_early_success = p.parse_with_early_success(5);
    zassert_equal(true, result_early_success.is_success());
    zassert_equal(false, result_early_success.is_intermediary());
    zassert_equal(false, result_early_success.is_failed());
    zassert_equal(42, result_early_success.value());
}

ZTEST(result_suite, test_result_chain_intermediary) {
    class Parser {
    public:
        struct ParserHandle {
            explicit ParserHandle(float v) : value(v) {}
            float value;
        };
        using ParseResult = spn::Result<float, std::string, ParserHandle>;

        Parser() {}

        ParseResult parse(float input) {
            return stage_entry(input).chain([this](ParserHandle handle) { return stage_n(handle); }
            ).chain([this](ParserHandle handle) {
                 return stage_n(handle);
             }).chain([this](ParserHandle handle) { return stage_final(handle); });
        }

    protected:
        ParseResult stage_entry(float input) { return ParseResult::intermediary(ParserHandle(input)); }

        ParseResult stage_n(ParserHandle handle) {
            if (handle.value == 0) {
                return ParseResult::failed("stage_double: value was 0");
            }
            return ParseResult::intermediary(ParserHandle{handle.value + 1});
        }

        ParseResult stage_final(ParserHandle handle) { return ParseResult(handle.value); }
    };
    Parser     p;
    const auto result = p.parse(5);

    zassert_equal(7, result.value());
}

ZTEST(result_suite, test_result_chain_single_type) {
    class Parser {
    public:
        struct Message {
            Message(const std::string& v) : value(v) {}
            Message(const std::string&& v) : value(v) {}
            std::string value;
        };
        using ParseResult = Result<Message>;

        Parser() {}

        ParseResult parse(const std::string& input) {
            return stage_entry(input).chain([this](Message handle) { return stage_n(handle); }
            ).chain([this](Message handle) {
                 return stage_n(handle);
             }).chain([this](Message handle) { return stage_final(handle); });
        }

    protected:
        ParseResult stage_entry(const std::string& input) { return ParseResult::intermediary(Message(input)); }

        ParseResult stage_n(Message handle) {
            if (handle.value == "world") {
                return ParseResult::failed(Message("stage_n: value was 'world'"));
            }
            return ParseResult::intermediary(Message{handle.value + " world"});
        }

        ParseResult stage_final(Message handle) { return ParseResult(handle.value); }
    };
    Parser     p;
    const auto result = p.parse("hello");

    zassert_str_equal("hello world world", result.value().value.c_str());
}

ZTEST(result_suite, test_result_map) {
    using ResultType = Result<int, std::string, int>;

    auto result        = ResultType(10);
    auto mapped_result = result.map([](const int& value) { return value * 2; });

    zassert_true(mapped_result.is_success());
    zassert_equal(20, mapped_result.value());

    auto failed_result       = ResultType::failed("error");
    auto mapped_error_result = failed_result.map_error([](const std::string& err) { return err + " modified"; });

    zassert_true(mapped_error_result.is_failed());
    zassert_str_equal("error modified", mapped_error_result.error_value().c_str());
}

ZTEST(result_suite, test_result_map_intermediary) {
    using ResultType = Result<int, std::string, int>;

    auto intermediary_result = ResultType::intermediary(5);
    auto mapped_intermediary =
        intermediary_result.map_intermediary([](const int& intermediary) { return intermediary + 10; });

    zassert_true(mapped_intermediary.is_intermediary());
    zassert_equal(15, mapped_intermediary.intermediary_value());
}

ZTEST(result_suite, test_result_and_then) {
    using ResultType = Result<int, std::string, int>;

    auto result         = ResultType(10);
    auto chained_result = result.and_then([](const int& value) { return ResultType(value * 2); });

    zassert_true(chained_result.is_success());
    zassert_equal(20, chained_result.value());

    auto failed_result    = ResultType::failed("error");
    auto chained_on_error = failed_result.and_then([](const int& value) {
        return ResultType(value * 2); // should not be invoked
    });

    zassert_true(chained_on_error.is_failed());
    zassert_str_equal("error", chained_on_error.error_value().c_str());
}

ZTEST(result_suite, test_result_or_else) {
    using ResultType = Result<int, std::string, int>;

    auto failed_result    = ResultType::failed("error");
    auto recovered_result = failed_result.or_else([](const std::string& err) {
        return ResultType(42); // Fallback in case of error
    });

    zassert_true(recovered_result.is_success());
    zassert_equal(42, recovered_result.value());
}

ZTEST(result_suite, test_result_match) {
    using ResultType = Result<int, std::string, int>;

    auto result        = ResultType(10);
    int  matched_value = result.match(
        [](const int& value) { return value * 2; },
        [](const std::string& error) { return -1; },
        [](const int& intermediary) { return 0; }
    );
    zassert_equal(20, matched_value);

    auto failed_result        = ResultType::failed("error");
    int  matched_failed_value = failed_result.match(
        [](const int& value) { return value * 2; },
        [](const std::string& error) { return -1; },
        [](const int& intermediary) { return 0; }
    );
    zassert_equal(-1, matched_failed_value);
}

ZTEST(result_suite, test_result_value_or_error_or) {
    using ResultType = Result<int, std::string, int>;

    auto result        = ResultType::failed("error");
    int  default_value = result.value_or(42);
    zassert_equal(42, default_value);

    std::string default_error = result.error_or("default error");
    zassert_str_equal("error", default_error.c_str());
}

ZTEST(result_suite, test_result_unwrap_or_else) {
    using ResultType = Result<int, std::string, int>;

    auto result         = ResultType::failed("error");
    int  fallback_value = result.unwrap_or_else([]() { return 42; });
    zassert_equal(42, fallback_value);
}

ZTEST(result_suite, test_result_no_value_state) {
    using ResultType = Result<int, std::string, int>;

    // Test default constructor creates NO_VALUE state
    ResultType default_result;
    zassert_false(default_result.is_success());
    zassert_false(default_result.is_failed());
    zassert_false(default_result.is_intermediary());
    zassert_false(default_result);

    // Test that operations on NO_VALUE state behave correctly
    auto mapped_result = default_result.map([](const int& value) { return value * 2; });
    zassert_false(mapped_result.is_success());
    zassert_false(mapped_result.is_failed());
    zassert_false(mapped_result.is_intermediary());

    auto mapped_error = default_result.map_error([](const std::string& err) { return err + " modified"; });
    zassert_false(mapped_error.is_success());
    zassert_false(mapped_error.is_failed());
    zassert_false(mapped_error.is_intermediary());

    auto mapped_intermediary = default_result.map_intermediary([](const int& inter) { return inter + 10; });
    zassert_false(mapped_intermediary.is_success());
    zassert_false(mapped_intermediary.is_failed());
    zassert_false(mapped_intermediary.is_intermediary());

    // Test value_or with NO_VALUE state
    int fallback_value = default_result.value_or(99);
    zassert_equal(99, fallback_value);

    std::string fallback_error = default_result.error_or("default");
    zassert_str_equal("default", fallback_error.c_str());
}

ZTEST(result_suite, test_result_copy_semantics) {
    using ResultType = Result<int, std::string, int>;

    // Test copy constructor with success state
    ResultType success_original(42);
    ResultType success_copy(success_original);
    zassert_true(success_copy.is_success());
    zassert_equal(42, success_copy.value());
    zassert_true(success_original.is_success()); // Original should remain valid

    // Test copy constructor with failed state
    ResultType failed_original = ResultType::failed("test error");
    ResultType failed_copy(failed_original);
    zassert_true(failed_copy.is_failed());
    zassert_str_equal("test error", failed_copy.error_value().c_str());
    zassert_true(failed_original.is_failed()); // Original should remain valid

    // Test copy constructor with intermediary state
    ResultType intermediary_original = ResultType::intermediary(123);
    ResultType intermediary_copy(intermediary_original);
    zassert_true(intermediary_copy.is_intermediary());
    zassert_equal(123, intermediary_copy.intermediary_value());
    zassert_true(intermediary_original.is_intermediary()); // Original should remain valid

    // Test copy assignment operator
    ResultType target;
    target = success_original;
    zassert_true(target.is_success());
    zassert_equal(42, target.value());

    // Test self-assignment (should be safe)
    target = target;
    zassert_true(target.is_success());
    zassert_equal(42, target.value());

    // Test move assignment after copy
    ResultType move_target;
    move_target = etl::move(target);
    zassert_true(move_target.is_success());
    zassert_equal(42, move_target.value());
}

ZTEST(result_suite, test_result_const_factories) {
    using ResultType = Result<int, std::string, int>;

    // Test const reference factory methods
    const int intermediary_value  = 567;
    auto      intermediary_result = ResultType::intermediary(intermediary_value);
    zassert_true(intermediary_result.is_intermediary());
    zassert_equal(567, intermediary_result.intermediary_value());

    const std::string error_value   = "const error";
    auto              failed_result = ResultType::failed(error_value);
    zassert_true(failed_result.is_failed());
    zassert_str_equal("const error", failed_result.error_value().c_str());

    // Test const reference constructor
    const int  success_value = 789;
    ResultType success_result(success_value);
    zassert_true(success_result.is_success());
    zassert_equal(789, success_result.value());

    // Test that const factory methods work with temporaries too
    auto temp_intermediary = ResultType::intermediary(999);
    zassert_true(temp_intermediary.is_intermediary());
    zassert_equal(999, temp_intermediary.intermediary_value());

    auto temp_failed = ResultType::failed(std::string("temp error"));
    zassert_true(temp_failed.is_failed());
    zassert_str_equal("temp error", temp_failed.error_value().c_str());
}

ZTEST(result_suite, test_result_tagged_constructors) {
    using ResultType = Result<int, std::string, int>;

    // Test tagged ok constructor
    auto success_tagged = ResultType(ResultType::ok, 100);
    zassert_true(success_tagged.is_success());
    zassert_equal(100, success_tagged.value());

    // Test tagged err constructor
    auto error_tagged = ResultType(ResultType::err, std::string("tagged error"));
    zassert_true(error_tagged.is_failed());
    zassert_str_equal("tagged error", error_tagged.error_value().c_str());

    // Test tagged mid constructor
    auto intermediary_tagged = ResultType(ResultType::mid, 250);
    zassert_true(intermediary_tagged.is_intermediary());
    zassert_equal(250, intermediary_tagged.intermediary_value());

    // Test tagged constructors with rvalue references
    auto success_rvalue = ResultType(ResultType::ok, 200);
    zassert_true(success_rvalue.is_success());
    zassert_equal(200, success_rvalue.value());

    auto error_rvalue = ResultType(ResultType::err, std::string("rvalue error"));
    zassert_true(error_rvalue.is_failed());
    zassert_str_equal("rvalue error", error_rvalue.error_value().c_str());

    auto intermediary_rvalue = ResultType(ResultType::mid, 350);
    zassert_true(intermediary_rvalue.is_intermediary());
    zassert_equal(350, intermediary_rvalue.intermediary_value());
}

ZTEST(result_suite, test_result_accessor_edge_cases) {
    using ResultType = Result<int, std::string, int>;

    // Test accessing success value from different states
    ResultType success_result(42);
    zassert_equal(42, success_result.value());
    zassert_equal(42, *success_result);
    zassert_equal(42, *(success_result.operator->()));

    // Test accessing error value from failed state
    auto failed_result = ResultType::failed("error message");
    zassert_str_equal("error message", failed_result.error_value().c_str());

    // Test accessing intermediary value from intermediary state
    auto intermediary_result = ResultType::intermediary(123);
    zassert_equal(123, intermediary_result.intermediary_value());

    // Test bool conversion
    zassert_true(success_result);
    zassert_false(failed_result);
    zassert_false(intermediary_result);

    ResultType no_value_result;
    zassert_false(no_value_result);

    // Test state checking methods across all states
    zassert_true(success_result.is_success());
    zassert_false(success_result.is_failed());
    zassert_false(success_result.is_intermediary());

    zassert_false(failed_result.is_success());
    zassert_true(failed_result.is_failed());
    zassert_false(failed_result.is_intermediary());

    zassert_false(intermediary_result.is_success());
    zassert_false(intermediary_result.is_failed());
    zassert_true(intermediary_result.is_intermediary());

    zassert_false(no_value_result.is_success());
    zassert_false(no_value_result.is_failed());
    zassert_false(no_value_result.is_intermediary());
}

ZTEST(result_suite, test_result_unwrap_methods) {
    using ResultType = Result<int, std::string, int>;

    // Test unwrap from success state
    ResultType success_result(42);
    int        unwrapped_value = success_result.unwrap();
    zassert_equal(42, unwrapped_value);
    zassert_false(success_result.is_success()); // Should be NO_VALUE after unwrap

    // Test unwrap_error_value from failed state
    auto        failed_result   = ResultType::failed("error message");
    std::string unwrapped_error = failed_result.unwrap_error_value();
    zassert_str_equal("error message", unwrapped_error.c_str());
    zassert_false(failed_result.is_failed()); // Should be NO_VALUE after unwrap

    // Test unwrap_intermediary_value from intermediary state
    auto intermediary_result    = ResultType::intermediary(123);
    int  unwrapped_intermediary = intermediary_result.unwrap_intermediary_value();
    zassert_equal(123, unwrapped_intermediary);
    zassert_false(intermediary_result.is_intermediary()); // Should be NO_VALUE after unwrap

    // Test unwrap_or_else with success
    ResultType success_for_fallback(99);
    int        fallback_result = success_for_fallback.unwrap_or_else([]() { return 0; });
    zassert_equal(99, fallback_result);

    // Test unwrap_or_else with non-success
    ResultType failed_for_fallback = ResultType::failed("error");
    int        fallback_computed   = failed_for_fallback.unwrap_or_else([]() { return 500; });
    zassert_equal(500, fallback_computed);

    auto intermediary_for_fallback = ResultType::intermediary(200);
    int  fallback_intermediary     = intermediary_for_fallback.unwrap_or_else([]() { return 600; });
    zassert_equal(600, fallback_intermediary);

    ResultType no_value_for_fallback;
    int        fallback_no_value = no_value_for_fallback.unwrap_or_else([]() { return 700; });
    zassert_equal(700, fallback_no_value);
}

ZTEST(result_suite, test_result_map_all_states) {
    using ResultType = Result<int, std::string, int>;

    // Test map with success state
    ResultType success_result(10);
    auto       mapped_success = success_result.map([](const int& val) { return val * 2; });
    zassert_true(mapped_success.is_success());
    zassert_equal(20, mapped_success.value());

    // Test map with failed state (should propagate error)
    auto failed_result = ResultType::failed("error");
    auto mapped_failed = failed_result.map([](const int& val) { return val * 2; });
    zassert_true(mapped_failed.is_failed());
    zassert_str_equal("error", mapped_failed.error_value().c_str());

    // Test map with intermediary state (should propagate intermediary)
    auto intermediary_result = ResultType::intermediary(15);
    auto mapped_intermediary = intermediary_result.map([](const int& val) { return val * 2; });
    zassert_true(mapped_intermediary.is_intermediary());
    zassert_equal(15, mapped_intermediary.intermediary_value());

    // Test map_error with failed state
    auto map_error_result = failed_result.map_error([](const std::string& err) { return err + " mapped"; });
    zassert_true(map_error_result.is_failed());
    zassert_str_equal("error mapped", map_error_result.error_value().c_str());

    // Test map_error with success state (should propagate success)
    auto map_error_success = success_result.map_error([](const std::string& err) { return err + " mapped"; });
    zassert_true(map_error_success.is_success());
    zassert_equal(10, map_error_success.value());

    // Test map_intermediary with intermediary state
    auto map_inter_result = intermediary_result.map_intermediary([](const int& inter) { return inter + 100; });
    zassert_true(map_inter_result.is_intermediary());
    zassert_equal(115, map_inter_result.intermediary_value());

    // Test map_intermediary with success state (should propagate success)
    auto map_inter_success = success_result.map_intermediary([](const int& inter) { return inter + 100; });
    zassert_true(map_inter_success.is_success());
    zassert_equal(10, map_inter_success.value());

    // Test all map functions with NO_VALUE state
    ResultType no_value_result;
    auto       map_no_value = no_value_result.map([](const int& val) { return val * 2; });
    zassert_false(map_no_value.is_success());
    zassert_false(map_no_value.is_failed());
    zassert_false(map_no_value.is_intermediary());

    auto map_error_no_value = no_value_result.map_error([](const std::string& err) { return err + " mapped"; });
    zassert_false(map_error_no_value.is_success());
    zassert_false(map_error_no_value.is_failed());
    zassert_false(map_error_no_value.is_intermediary());

    auto map_inter_no_value = no_value_result.map_intermediary([](const int& inter) { return inter + 100; });
    zassert_false(map_inter_no_value.is_success());
    zassert_false(map_inter_no_value.is_failed());
    zassert_false(map_inter_no_value.is_intermediary());
}

ZTEST(result_suite, test_result_pattern_match_complete) {
    using ResultType = Result<int, std::string, int>;

    // Test match with success state
    ResultType success_result(42);
    auto       success_match_result = success_result.match(
        [](const int& val) { return val * 2; },    // success function
        [](const std::string& err) { return -1; }, // error function
        [](const int& inter) { return -2; }        // intermediary function
    );
    zassert_equal(84, success_match_result);

    // Test match with failed state
    auto failed_result       = ResultType::failed("test error");
    auto failed_match_result = failed_result.match(
        [](const int& val) { return val * 2; },                   // success function
        [](const std::string& err) { return (int)err.length(); }, // error function
        [](const int& inter) { return -2; }                       // intermediary function
    );
    zassert_equal(10, failed_match_result); // "test error" has 10 characters

    // Test match with intermediary state
    auto intermediary_result       = ResultType::intermediary(15);
    auto intermediary_match_result = intermediary_result.match(
        [](const int& val) { return val * 2; },      // success function
        [](const std::string& err) { return -1; },   // error function
        [](const int& inter) { return inter + 100; } // intermediary function
    );
    zassert_equal(115, intermediary_match_result);

    // Test match with different return types for better coverage
    struct MatchResult {
        enum Type { SUCCESS, FAILED, INTERMEDIARY } type;
        int value;
    };

    auto success_type_match = success_result.match(
        [](const int& val) {
            return MatchResult{MatchResult::SUCCESS, val};
        },
        [](const std::string& err) {
            return MatchResult{MatchResult::FAILED, 0};
        },
        [](const int& inter) {
            return MatchResult{MatchResult::INTERMEDIARY, inter};
        }
    );
    zassert_equal(MatchResult::SUCCESS, success_type_match.type);
    zassert_equal(42, success_type_match.value);

    auto failed_type_match = failed_result.match(
        [](const int& val) {
            return MatchResult{MatchResult::SUCCESS, val};
        },
        [](const std::string& err) {
            return MatchResult{MatchResult::FAILED, (int)err.length()};
        },
        [](const int& inter) {
            return MatchResult{MatchResult::INTERMEDIARY, inter};
        }
    );
    zassert_equal(MatchResult::FAILED, failed_type_match.type);
    zassert_equal(10, failed_type_match.value);

    auto intermediary_type_match = intermediary_result.match(
        [](const int& val) {
            return MatchResult{MatchResult::SUCCESS, val};
        },
        [](const std::string& err) {
            return MatchResult{MatchResult::FAILED, 0};
        },
        [](const int& inter) {
            return MatchResult{MatchResult::INTERMEDIARY, inter};
        }
    );
    zassert_equal(MatchResult::INTERMEDIARY, intermediary_type_match.type);
    zassert_equal(15, intermediary_type_match.value);
}

ZTEST(result_suite, test_result_value_or_with_success) {
    using ResultType = Result<int, std::string, int>;

    // Test value_or with success state
    ResultType success_result(42);
    int        success_value_or = success_result.value_or(99);
    zassert_equal(42, success_value_or); // Should return actual value, not fallback

    // Test value_or with failed state
    auto failed_result   = ResultType::failed("error");
    int  failed_value_or = failed_result.value_or(99);
    zassert_equal(99, failed_value_or); // Should return fallback

    // Test value_or with intermediary state
    auto intermediary_result   = ResultType::intermediary(123);
    int  intermediary_value_or = intermediary_result.value_or(99);
    zassert_equal(99, intermediary_value_or); // Should return fallback

    // Test value_or with NO_VALUE state
    ResultType no_value_result;
    int        no_value_value_or = no_value_result.value_or(99);
    zassert_equal(99, no_value_value_or); // Should return fallback

    // Test error_or with failed state
    std::string failed_error_or = failed_result.error_or("default");
    zassert_str_equal("error", failed_error_or.c_str()); // Should return actual error

    // Test error_or with success state
    std::string success_error_or = success_result.error_or("default");
    zassert_str_equal("default", success_error_or.c_str()); // Should return fallback

    // Test error_or with intermediary state
    std::string intermediary_error_or = intermediary_result.error_or("default");
    zassert_str_equal("default", intermediary_error_or.c_str()); // Should return fallback

    // Test error_or with NO_VALUE state
    std::string no_value_error_or = no_value_result.error_or("default");
    zassert_str_equal("default", no_value_error_or.c_str()); // Should return fallback

    // Test value_or with different types (testing template flexibility)
    auto success_value_or_double = success_result.value_or(3.14);
    zassert_equal(42, success_value_or_double); // Should convert int to int

    auto failed_value_or_double = failed_result.value_or(3.14);
    zassert_equal(3, failed_value_or_double); // Should convert double to int

    // Test error_or with different string types
    auto failed_error_or_literal = failed_result.error_or("literal");
    zassert_str_equal("error", failed_error_or_literal.c_str());

    auto success_error_or_literal = success_result.error_or("literal");
    zassert_str_equal("literal", success_error_or_literal.c_str());
}

ZTEST(result_suite, test_result_operator_arrow) {
    struct TestStruct {
        int         value;
        std::string name;

        int                get_value() const { return value; }
        const std::string& get_name() const { return name; }
    };

    using ResultType = Result<TestStruct, std::string, TestStruct>;

    // Test operator-> with success state
    TestStruct test_obj{42, "test"};
    ResultType success_result(test_obj);

    // Test arrow operator access to members
    zassert_equal(42, success_result->value);
    zassert_str_equal("test", success_result->name.c_str());

    // Test arrow operator access to methods
    zassert_equal(42, success_result->get_value());
    zassert_str_equal("test", success_result->get_name().c_str());

    // Test dereference operator
    TestStruct dereferenced = *success_result;
    zassert_equal(42, dereferenced.value);
    zassert_str_equal("test", dereferenced.name.c_str());

    // Test const arrow operator
    const ResultType const_success_result(test_obj);
    zassert_equal(42, const_success_result->value);
    zassert_str_equal("test", const_success_result->name.c_str());

    // Test const dereference operator
    const TestStruct const_dereferenced = *const_success_result;
    zassert_equal(42, const_dereferenced.value);
    zassert_str_equal("test", const_dereferenced.name.c_str());

    // Test with modified struct
    TestStruct modified_obj{99, "modified"};
    ResultType modified_result(modified_obj);
    zassert_equal(99, modified_result->value);
    zassert_str_equal("modified", modified_result->name.c_str());
    zassert_equal(99, modified_result->get_value());
    zassert_str_equal("modified", modified_result->get_name().c_str());
}

ZTEST(result_suite, test_result_and_then_or_else_complete) {
    using ResultType = Result<int, std::string, int>;

    // Test and_then with success state - should call the function
    ResultType success_result(10);
    auto       and_then_success = success_result.and_then([](const int& val) { return ResultType(val * 2); });
    zassert_true(and_then_success.is_success());
    zassert_equal(20, and_then_success.value());

    // Test and_then with success state returning failed
    auto and_then_success_to_fail = success_result.and_then([](const int& val) {
        if (val > 5) {
            return ResultType::failed("value too large");
        }
        return ResultType(val * 2);
    });
    zassert_true(and_then_success_to_fail.is_failed());
    zassert_str_equal("value too large", and_then_success_to_fail.error_value().c_str());

    // Test and_then with success state returning intermediary
    auto and_then_success_to_inter =
        success_result.and_then([](const int& val) { return ResultType::intermediary(val + 100); });
    zassert_true(and_then_success_to_inter.is_intermediary());
    zassert_equal(110, and_then_success_to_inter.intermediary_value());

    // Test and_then with failed state - should propagate error
    auto failed_result   = ResultType::failed("initial error");
    auto and_then_failed = failed_result.and_then([](const int& val) { return ResultType(val * 2); });
    zassert_true(and_then_failed.is_failed());
    zassert_str_equal("initial error", and_then_failed.error_value().c_str());

    // Test and_then with intermediary state - should propagate intermediary
    auto intermediary_result   = ResultType::intermediary(25);
    auto and_then_intermediary = intermediary_result.and_then([](const int& val) { return ResultType(val * 2); });
    zassert_true(and_then_intermediary.is_intermediary());
    zassert_equal(25, and_then_intermediary.intermediary_value());

    // Test and_then with NO_VALUE state - should return NO_VALUE
    ResultType no_value_result;
    auto       and_then_no_value = no_value_result.and_then([](const int& val) { return ResultType(val * 2); });
    zassert_false(and_then_no_value.is_success());
    zassert_false(and_then_no_value.is_failed());
    zassert_false(and_then_no_value.is_intermediary());

    // Test or_else with failed state - should call the function
    auto or_else_failed = failed_result.or_else([](const std::string& err) {
        return ResultType(42); // Recovery value
    });
    zassert_true(or_else_failed.is_success());
    zassert_equal(42, or_else_failed.value());

    // Test or_else with failed state returning another failure
    auto or_else_failed_to_fail =
        failed_result.or_else([](const std::string& err) { return ResultType::failed("recovery failed: " + err); });
    zassert_true(or_else_failed_to_fail.is_failed());
    zassert_str_equal("recovery failed: initial error", or_else_failed_to_fail.error_value().c_str());

    // Test or_else with success state - should propagate success
    auto or_else_success = success_result.or_else([](const std::string& err) {
        return ResultType(999); // Should not be called
    });
    zassert_true(or_else_success.is_success());
    zassert_equal(10, or_else_success.value());

    // Test or_else with intermediary state - should propagate intermediary
    auto or_else_intermediary = intermediary_result.or_else([](const std::string& err) {
        return ResultType(999); // Should not be called
    });
    zassert_true(or_else_intermediary.is_intermediary());
    zassert_equal(25, or_else_intermediary.intermediary_value());

    // Test or_else with NO_VALUE state - should return NO_VALUE
    auto or_else_no_value = no_value_result.or_else([](const std::string& err) {
        return ResultType(999); // Should not be called
    });
    zassert_false(or_else_no_value.is_success());
    zassert_false(or_else_no_value.is_failed());
    zassert_false(or_else_no_value.is_intermediary());
}