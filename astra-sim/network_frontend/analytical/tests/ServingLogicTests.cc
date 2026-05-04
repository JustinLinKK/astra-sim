/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "astra-sim/workload/ServingConfig.hh"
#include "astra-sim/workload/ServingUtils.hh"

using namespace AstraSim;

namespace {

void expect_true(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_equal(uint64_t lhs, uint64_t rhs, const std::string& message) {
    if (lhs != rhs) {
        throw std::runtime_error(message + ": expected " + std::to_string(rhs) +
                                 ", got " + std::to_string(lhs));
    }
}

void expect_close(double lhs, double rhs, double tolerance,
                  const std::string& message) {
    if (std::fabs(lhs - rhs) > tolerance) {
        throw std::runtime_error(message + ": expected " + std::to_string(rhs) +
                                 ", got " + std::to_string(lhs));
    }
}

void test_explicit_config() {
    const auto config = ServingConfig::load_from_json_text(
        R"json(
        {
          "baseline": {
            "prefill_base_latency_ns": 5,
            "prefill_compute_ns_per_token": 7,
            "decode_base_latency_ns": 11,
            "decode_compute_ns_per_token": 13,
            "prefill_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0},
            "decode_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0}
          },
          "requests": [
            {"request_id": 9, "arrival_time_ns": 10, "prompt_tokens": 12, "output_tokens": 14}
          ]
        }
        )json",
        "<explicit>");

    expect_equal(config.prefill_base_latency_ns, 5,
                 "prefill base latency should parse");
    expect_equal(config.decode_base_latency_ns, 11,
                 "decode base latency should parse");
    expect_true(!config.trace_seed.has_value(),
                "explicit request configs should not report a seed");
    expect_equal(config.requests.size(), 1,
                 "explicit request configs should retain one request");
    expect_equal(config.requests.front().request_id, 9,
                 "explicit request id should parse");
}

void test_generated_trace_determinism() {
    const std::string trace_json = R"json(
        {
          "baseline": {
            "prefill_base_latency_ns": 0,
            "prefill_compute_ns_per_token": 1000,
            "decode_base_latency_ns": 0,
            "decode_compute_ns_per_token": 500,
            "prefill_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0},
            "decode_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0}
          },
          "trace": {
            "seed": 42,
            "num_requests": 32,
            "arrival_process": "poisson",
            "request_rate_per_second": 2.5,
            "prompt_tokens": {"distribution": "uniform", "min": 8, "max": 16},
            "output_tokens": {"distribution": "uniform", "min": 3, "max": 6}
          }
        }
    )json";

    const auto config_a =
        ServingConfig::load_from_json_text(trace_json, "<trace-a>");
    const auto config_b =
        ServingConfig::load_from_json_text(trace_json, "<trace-b>");

    expect_true(config_a.trace_seed.has_value() && config_b.trace_seed.has_value(),
                "generated configs should report their seed");
    expect_equal(*config_a.trace_seed, 42, "trace seed should parse");
    expect_equal(config_a.requests.size(), config_b.requests.size(),
                 "same seed should generate same request count");

    Tick previous_arrival = 0;
    for (size_t index = 0; index < config_a.requests.size(); ++index) {
        const auto& lhs = config_a.requests[index];
        const auto& rhs = config_b.requests[index];
        expect_equal(lhs.request_id, rhs.request_id,
                     "same seed should generate same request ids");
        expect_equal(lhs.arrival_time_ns, rhs.arrival_time_ns,
                     "same seed should generate same arrivals");
        expect_equal(lhs.prompt_tokens, rhs.prompt_tokens,
                     "same seed should generate same prompt lengths");
        expect_equal(lhs.output_tokens, rhs.output_tokens,
                     "same seed should generate same output lengths");
        expect_true(lhs.arrival_time_ns >= previous_arrival,
                    "generated arrivals should be non-decreasing");
        expect_true(lhs.prompt_tokens >= 8 && lhs.prompt_tokens <= 16,
                    "prompt tokens should stay within configured bounds");
        expect_true(lhs.output_tokens >= 3 && lhs.output_tokens <= 6,
                    "output tokens should stay within configured bounds");
        previous_arrival = lhs.arrival_time_ns;
    }
}

void test_invalid_configs() {
    bool saw_invalid_both = false;
    try {
        (void)ServingConfig::load_from_json_text(
            R"json(
            {
              "baseline": {
                "prefill_base_latency_ns": 0,
                "prefill_compute_ns_per_token": 1,
                "decode_base_latency_ns": 0,
                "decode_compute_ns_per_token": 1,
                "prefill_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0},
                "decode_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0}
              },
              "requests": [],
              "trace": {
                "seed": 1,
                "num_requests": 1,
                "arrival_process": "poisson",
                "request_rate_per_second": 1.0,
                "prompt_tokens": {"distribution": "uniform", "min": 1, "max": 1},
                "output_tokens": {"distribution": "uniform", "min": 1, "max": 1}
              }
            }
            )json",
            "<invalid-both>");
    } catch (const std::runtime_error&) {
        saw_invalid_both = true;
    }
    expect_true(saw_invalid_both,
                "configs with both requests and trace should be rejected");

    bool saw_zero_prompt = false;
    try {
        (void)ServingConfig::load_from_json_text(
            R"json(
            {
              "baseline": {
                "prefill_base_latency_ns": 0,
                "prefill_compute_ns_per_token": 1,
                "decode_base_latency_ns": 0,
                "decode_compute_ns_per_token": 1,
                "prefill_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0},
                "decode_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0}
              },
              "requests": [
                {"request_id": 0, "arrival_time_ns": 0, "prompt_tokens": 0, "output_tokens": 1}
              ]
            }
            )json",
            "<invalid-prompt>");
    } catch (const std::runtime_error&) {
        saw_zero_prompt = true;
    }
    expect_true(saw_zero_prompt,
                "configs with zero prompt tokens should be rejected");
}

void test_duration_math() {
    const auto short_prefill =
        calculate_scaled_serving_duration(10, 4, 20, 1.0);
    const auto long_prefill =
        calculate_scaled_serving_duration(10, 8, 20, 1.0);
    const auto short_decode =
        calculate_scaled_serving_duration(5, 1, 30, 1.0);
    const auto long_decode =
        calculate_scaled_serving_duration(5, 1, 60, 1.0);

    expect_true(long_prefill > short_prefill,
                "prefill duration should increase with prompt tokens");
    expect_true(long_decode > short_decode,
                "decode duration should increase with per-token latency");
    expect_equal(short_prefill, 90,
                 "prefill duration should include base latency");
    expect_equal(short_decode, 35,
                 "decode duration should include base latency");
}

void test_tpot_and_summary_math() {
    expect_close(calculate_serving_tpot_ns(100, 4), 25.0, 1e-9,
                 "TPOT should use total decode duration divided by output tokens");
    expect_close(calculate_serving_tpot_ns(0, 4), 0.0, 1e-9,
                 "zero decode duration should produce zero TPOT");

    const auto stats = summarize_serving_metric({1.0, 2.0, 3.0, 4.0, 5.0});
    expect_equal(stats.count, 5, "summary stats should preserve count");
    expect_close(stats.mean, 3.0, 1e-9, "summary mean should match");
    expect_close(stats.p50, 3.0, 1e-9, "summary p50 should match");
    expect_close(stats.p90, 5.0, 1e-9, "summary p90 should match");
    expect_close(stats.p99, 5.0, 1e-9, "summary p99 should match");
    expect_close(stats.max, 5.0, 1e-9, "summary max should match");

    expect_close(calculate_throughput_per_second(4, 2000000000ULL), 2.0, 1e-9,
                 "throughput should divide count by makespan");
    expect_close(calculate_throughput_per_second(10, 0), 0.0, 1e-9,
                 "zero makespan should yield zero throughput");
}

}  // namespace

int main() {
    try {
        test_explicit_config();
        test_generated_trace_determinism();
        test_invalid_configs();
        test_duration_math();
        test_tpot_and_summary_math();
    } catch (const std::exception& error) {
        std::cerr << "Serving logic test failure: " << error.what() << "\n";
        return 1;
    }

    std::cout << "Serving logic tests passed.\n";
    return 0;
}
