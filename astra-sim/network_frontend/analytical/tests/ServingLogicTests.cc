/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "astra-sim/workload/ServingConfig.hh"
#include "astra-sim/workload/ServingCostModel.hh"
#include "astra-sim/workload/ServingRuntime.hh"
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

void test_explicit_legacy_config() {
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

    expect_equal(config.cost_model.prefill_base_latency_ns, 5,
                 "prefill base latency should parse");
    expect_equal(config.cost_model.decode_base_latency_ns, 11,
                 "decode base latency should parse");
    expect_true(config.runtime.architecture ==
                    ServingArchitecture::SerialBaseline,
                "legacy configs should default to serial_baseline");
    expect_true(config.scheduler.scheduler_policy ==
                    ServingSchedulerPolicy::Serial,
                "legacy configs should default to serial scheduling");
    expect_true(!config.slo.enabled,
                "legacy configs should not enable SLOs");
    expect_true(!config.trace_seed.has_value(),
                "explicit request configs should not report a trace seed");
    expect_equal(config.requests.size(), 1,
                 "explicit request configs should retain one request");
    expect_equal(config.requests.front().request_id, 9,
                 "explicit request id should parse");
}

void test_runtime_scheduler_and_slo_config() {
    const auto config = ServingConfig::load_from_json_text(
        R"json(
        {
          "runtime": {"architecture": "colocated_chunked", "seed": 17},
          "scheduler": {
            "max_running_requests": 32,
            "scheduler_policy": "decode_first",
            "max_prefill_batch_tokens": 4096,
            "max_decode_batch_requests": 16,
            "chunked_prefill_size": 1024,
            "prefill_max_requests": 8,
            "enable_mixed_chunk": true
          },
          "outputs": {
            "event_trace_output": "events.csv",
            "stage_metrics_output": "stage_metrics.csv"
          },
          "slo": {
            "ttft_ns": 2000,
            "tpot_ns": 100.0,
            "e2e_ns": 3000
          },
          "baseline": {
            "prefill_base_latency_ns": 5,
            "prefill_compute_ns_per_token": 7,
            "decode_base_latency_ns": 11,
            "decode_compute_ns_per_token": 13,
            "prefill_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0},
            "decode_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0}
          },
          "requests": [
            {"request_id": 0, "arrival_time_ns": 0, "prompt_tokens": 12, "output_tokens": 14}
          ]
        }
        )json",
        "<runtime-slo>");

    expect_true(config.runtime.architecture ==
                    ServingArchitecture::ColocatedChunked,
                "runtime architecture should parse");
    expect_true(config.runtime.seed.has_value() && *config.runtime.seed == 17,
                "runtime seed should parse");
    expect_equal(config.scheduler.max_running_requests, 32,
                 "max_running_requests should parse");
    expect_true(config.scheduler.scheduler_policy ==
                    ServingSchedulerPolicy::DecodeFirst,
                "scheduler_policy should parse");
    expect_equal(config.scheduler.max_prefill_batch_tokens, 4096,
                 "max_prefill_batch_tokens should parse");
    expect_equal(config.scheduler.max_decode_batch_requests, 16,
                 "max_decode_batch_requests should parse");
    expect_equal(config.scheduler.chunked_prefill_size, 1024,
                 "chunked_prefill_size should parse");
    expect_equal(config.scheduler.prefill_max_requests, 8,
                 "prefill_max_requests should parse");
    expect_true(config.scheduler.enable_mixed_chunk,
                "enable_mixed_chunk should parse");
    expect_true(config.slo.enabled, "SLO should be enabled");
    expect_equal(config.slo.ttft_ns, 2000, "ttft SLO should parse");
    expect_close(config.slo.tpot_ns, 100.0, 1e-9,
                 "tpot SLO should parse");
    expect_true(config.slo.e2e_ns.has_value() && *config.slo.e2e_ns == 3000,
                "e2e SLO should parse");
    expect_true(config.outputs.event_trace_output.find("events.csv") !=
                    std::string::npos,
                "event trace output should parse");
    expect_true(config.outputs.stage_metrics_output.find("stage_metrics.csv") !=
                    std::string::npos,
                "stage metrics output should parse");
}

void test_calibration_fidelity_config() {
    const auto config = ServingConfig::load_from_json_text(
        R"json(
        {
          "runtime": {"architecture": "colocated"},
          "scheduler": {
            "scheduler_policy": "fcfs",
            "max_decode_batch_requests": 2
          },
          "benchmark_start_ns": 10,
          "benchmark_duration_ns": 5000,
          "cost_model": {
            "prefill_base_latency_ns": 0,
            "prefill_ns_per_token": 0,
            "decode_base_latency_ns": 0,
            "decode_ns_per_token": 10,
            "decode_step_latency_ns": 1000,
            "first_token_timing": "decode_start",
            "first_token_latency_ns": 50
          },
          "requests": [
            {"request_id": 0, "arrival_time_ns": 0, "prompt_tokens": 1, "output_tokens": 1},
            {"request_id": 1, "arrival_time_ns": 0, "prompt_tokens": 1, "output_tokens": 1}
          ]
        }
        )json",
        "<calibration-fidelity>");

    expect_true(config.scheduler.scheduler_policy == ServingSchedulerPolicy::Fcfs,
                "fcfs scheduler policy should parse");
    expect_true(config.benchmark_start_ns.has_value() &&
                    *config.benchmark_start_ns == 10,
                "benchmark_start_ns should parse");
    expect_true(config.benchmark_duration_ns.has_value() &&
                    *config.benchmark_duration_ns == 5000,
                "benchmark_duration_ns should parse");
    expect_equal(config.cost_model.decode_step_latency_ns, 1000,
                 "decode_step_latency_ns should parse");
    expect_equal(config.cost_model.first_token_latency_ns, 50,
                 "first_token_latency_ns should parse");
    expect_true(config.cost_model.first_token_timing ==
                    ServingFirstTokenTiming::DecodeStart,
                "first_token_timing should parse");

    ServingCostModel cost_model(config, 1.0, 1.0);
    ServingBatch batch;
    batch.items.push_back(ServingBatchItem{0, 1});
    batch.items.push_back(ServingBatchItem{1, 1});
    const auto breakdown = cost_model.estimate_decode_breakdown(batch);
    expect_equal(breakdown.step_latency_ns, 1000,
                 "decode step latency should be charged once per batch");
    expect_equal(breakdown.total_ns(), 1020,
                 "decode step latency should not scale by batch tokens");
}

void test_decode_step_latency_curve() {
    auto config = ServingConfig::load_from_json_text(
        R"json(
        {
          "runtime": {"architecture": "pd_disaggregated"},
          "target_request_rate_per_second": 6.0,
          "pd": {
            "prefill_workers": 1,
            "decode_workers": 1,
            "prefill_max_batch_tokens": 16,
            "prefill_max_requests": 4,
            "decode_max_batch_requests": 4
          },
          "cost_model": {
            "prefill_base_latency_ns": 0,
            "prefill_ns_per_token": 0,
            "decode_base_latency_ns": 0,
            "decode_ns_per_token": 10,
            "decode_step_latency_ns": 50,
            "first_token_latency_ns": 25,
            "decode_step_latency_curve": {
              "enabled": true,
              "signal": "target_request_rate_per_second",
              "interpolation": "linear",
              "extrapolation": "clamp",
              "points": [
                {"request_rate_per_second": 2.0, "decode_step_latency_ns": 100},
                {"request_rate_per_second": 4.0, "decode_step_latency_ns": 200},
                {"request_rate_per_second": 8.0, "decode_step_latency_ns": 600}
              ]
            },
            "first_token_backpressure_curve": {
              "enabled": true,
              "signal": "target_request_rate_per_second",
              "model": "arrival_rank_linear",
              "interpolation": "linear",
              "extrapolation": "clamp",
              "points": [
                {
                  "request_rate_per_second": 2.0,
                  "base_latency_ns": 10,
                  "knee_request_index": 1.0,
                  "latency_ns_per_request_after_knee": 2.0
                },
                {
                  "request_rate_per_second": 4.0,
                  "base_latency_ns": 20,
                  "knee_request_index": 2.0,
                  "latency_ns_per_request_after_knee": 4.0
                },
                {
                  "request_rate_per_second": 8.0,
                  "base_latency_ns": 60,
                  "knee_request_index": 6.0,
                  "latency_ns_per_request_after_knee": 8.0
                }
              ]
            }
          },
          "requests": [
            {"request_id": 0, "arrival_time_ns": 0, "prompt_tokens": 1, "output_tokens": 1}
          ]
        }
        )json",
        "<decode-curve>");

    expect_true(config.target_request_rate_per_second.has_value(),
                "target_request_rate_per_second should parse");
    expect_close(*config.target_request_rate_per_second, 6.0, 1e-9,
                 "target request rate should parse");
    expect_true(config.cost_model.decode_step_latency_curve.enabled,
                "decode step latency curve should parse");
    expect_equal(config.cost_model.decode_step_latency_curve.points.size(), 3,
                 "decode step latency curve points should parse");
    expect_true(config.cost_model.first_token_backpressure_curve.enabled,
                "first token backpressure curve should parse");
    expect_equal(config.cost_model.first_token_backpressure_curve.points.size(),
                 3, "first token backpressure curve points should parse");

    ServingCostModel cost_model(config, 1.0, 1.0);
    ServingBatch batch;
    batch.items.push_back(ServingBatchItem{0, 1});
    batch.items.push_back(ServingBatchItem{1, 1});
    const auto breakdown = cost_model.estimate_decode_breakdown(batch);
    expect_equal(breakdown.step_latency_ns, 400,
                 "decode step latency curve should interpolate");
    expect_equal(breakdown.total_ns(), 420,
                 "decode step latency curve should affect decode duration");
    ServingRequestState request_state;
    request_state.spec = config.requests.front();
    request_state.spec.original_index = 7;
    expect_equal(
        cost_model.estimate_first_token_latency_ns(request_state), 58,
        "first token backpressure curve should interpolate by request rank");

    config.target_request_rate_per_second = 10.0;
    ServingCostModel clamped_cost_model(config, 1.0, 1.0);
    const auto clamped_breakdown =
        clamped_cost_model.estimate_decode_breakdown(batch);
    expect_equal(clamped_breakdown.step_latency_ns, 600,
                 "decode step latency curve should clamp above its range");
    expect_equal(
        clamped_cost_model.estimate_first_token_latency_ns(request_state), 68,
        "first token backpressure curve should clamp above its range");

    const auto scalar_fallback_config = ServingConfig::load_from_json_text(
        R"json(
        {
          "runtime": {"architecture": "pd_disaggregated"},
          "pd": {
            "prefill_workers": 1,
            "decode_workers": 1,
            "prefill_max_batch_tokens": 16,
            "prefill_max_requests": 4,
            "decode_max_batch_requests": 4
          },
          "cost_model": {
            "prefill_base_latency_ns": 0,
            "prefill_ns_per_token": 0,
            "decode_base_latency_ns": 0,
            "decode_ns_per_token": 10,
            "decode_step_latency_ns": 50,
            "first_token_latency_ns": 25,
            "decode_step_latency_curve": {
              "enabled": true,
              "points": [
                {"request_rate_per_second": 2.0, "decode_step_latency_ns": 100},
                {"request_rate_per_second": 4.0, "decode_step_latency_ns": 200}
              ]
            },
            "first_token_backpressure_curve": {
              "enabled": true,
              "points": [
                {
                  "request_rate_per_second": 2.0,
                  "base_latency_ns": 100,
                  "knee_request_index": 1.0,
                  "latency_ns_per_request_after_knee": 2.0
                },
                {
                  "request_rate_per_second": 4.0,
                  "base_latency_ns": 200,
                  "knee_request_index": 2.0,
                  "latency_ns_per_request_after_knee": 4.0
                }
              ]
            }
          },
          "requests": [
            {"request_id": 0, "arrival_time_ns": 0, "prompt_tokens": 1, "output_tokens": 1}
          ]
        }
        )json",
        "<decode-curve-fallback>");

    ServingCostModel scalar_fallback_cost_model(scalar_fallback_config, 1.0,
                                                1.0);
    const auto scalar_fallback_breakdown =
        scalar_fallback_cost_model.estimate_decode_breakdown(batch);
    expect_equal(
        scalar_fallback_breakdown.step_latency_ns, 50,
        "enabled curve should fall back to scalar latency without a target rate");
    expect_equal(
        scalar_fallback_cost_model.estimate_first_token_latency_ns(request_state),
        25,
        "enabled first token curve should fall back to scalar latency without a target rate");
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

void test_goodput_math() {
    const ServingSloConfig enabled_slo{
        true,
        1000,
        100.0,
        2000,
    };

    const auto good = evaluate_serving_goodput(enabled_slo, 900, 99.0, 1999);
    expect_true(good.slo_configured, "SLO-enabled result should report SLOs");
    expect_true(good.request_good, "meeting every threshold should be good");
    expect_true(good.fail_reason.empty(),
                "good requests should not have a fail reason");

    const auto ttft_bad =
        evaluate_serving_goodput(enabled_slo, 1001, 99.0, 1999);
    expect_true(!ttft_bad.request_good && !ttft_bad.ttft_pass,
                "TTFT violation should fail goodput");
    expect_true(ttft_bad.fail_reason == "ttft",
                "TTFT-only failure should report ttft");

    const auto tpot_bad =
        evaluate_serving_goodput(enabled_slo, 999, 100.1, 1999);
    expect_true(!tpot_bad.request_good && !tpot_bad.tpot_pass,
                "TPOT violation should fail goodput");
    expect_true(tpot_bad.fail_reason == "tpot",
                "TPOT-only failure should report tpot");

    const auto both_bad =
        evaluate_serving_goodput(enabled_slo, 1001, 101.0, 2500);
    expect_true(!both_bad.request_good && !both_bad.ttft_pass &&
                    !both_bad.tpot_pass && !both_bad.e2e_pass,
                "multiple failures should all be tracked");
    expect_true(both_bad.fail_reason == "ttft+tpot+e2e",
                "multiple failures should join reasons deterministically");

    const ServingSloConfig disabled_slo{};
    const auto disabled =
        evaluate_serving_goodput(disabled_slo, 999999, 999999.0, 999999);
    expect_true(!disabled.slo_configured && disabled.request_good,
                "disabled SLO should treat every request as good");
}

void test_transfer_model_math() {
    const auto config = ServingConfig::load_from_json_text(
        R"json(
        {
          "runtime": {"architecture": "pd_disaggregated"},
          "model": {
            "name": "toy",
            "num_layers": 80,
            "hidden_size": 8192,
            "attention_heads": 64,
            "kv_heads": 8,
            "head_dim": 128,
            "bytes_per_kv_element": 2
          },
          "pd": {
            "prefill_workers": 1,
            "decode_workers": 1,
            "decode_max_batch_requests": 4,
            "prefill_max_requests": 4,
            "prefill_max_batch_tokens": 8192,
            "transfer": {
              "enabled": true,
              "latency_ns": 3000,
              "bandwidth_bytes_per_s": 50000000000.0,
              "efficiency": 0.5
            }
          },
          "baseline": {
            "prefill_base_latency_ns": 0,
            "prefill_compute_ns_per_token": 1000,
            "decode_base_latency_ns": 0,
            "decode_compute_ns_per_token": 500,
            "prefill_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0},
            "decode_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0}
          },
          "requests": [
            {"request_id": 0, "arrival_time_ns": 0, "prompt_tokens": 2, "output_tokens": 4}
          ]
        }
        )json",
        "<transfer>");

    ServingRequestState request_state;
    request_state.spec = config.requests.front();
    ServingCostModel cost_model(config, 1.0, 1.0);

    const auto expected_bytes_per_prompt_token =
        80ULL * 8ULL * 128ULL * 2ULL * 2ULL;
    expect_equal(cost_model.estimate_kv_transfer_bytes(request_state),
                 2ULL * expected_bytes_per_prompt_token,
                 "transfer bytes should derive from model shape");

    const auto transfer_ns = cost_model.estimate_transfer_ns(request_state);
    expect_true(transfer_ns > 3000,
                "transfer duration should include latency and bandwidth");
}

void test_topology_and_component_config() {
    const auto config = ServingConfig::load_from_json_text(
        R"json(
        {
          "runtime": {"architecture": "pd_disaggregated"},
          "topology": {
            "deployment": "pd_disaggregated",
            "colocated_layout": {
              "name": "unused-colo",
              "tp_degree": 1,
              "pp_degree": 1,
              "ep_degree": 1,
              "dp_attention_degree": 1,
              "dp_replica_count": 2
            },
            "prefill_layout": {
              "name": "prefill-layout",
              "tp_degree": 2,
              "pp_degree": 1,
              "ep_degree": 1,
              "dp_attention_degree": 1,
              "dp_replica_count": 2
            },
            "decode_layout": {
              "name": "decode-layout",
              "tp_degree": 4,
              "pp_degree": 2,
              "ep_degree": 8,
              "dp_attention_degree": 4,
              "dp_replica_count": 2
            }
          },
          "model": {
            "name": "moe-toy",
            "kind": "moe",
            "num_layers": 32,
            "hidden_size": 4096,
            "attention_heads": 32,
            "kv_heads": 8,
            "head_dim": 128,
            "experts_per_layer": 8,
            "active_experts_per_token": 2,
            "expert_hidden_size": 14336,
            "bytes_per_activation": 2,
            "bytes_per_kv_element": 2
          },
          "pd": {
            "prefill_workers": 2,
            "decode_workers": 2,
            "prefill_max_batch_tokens": 1024,
            "prefill_max_requests": 4,
            "decode_max_batch_requests": 8,
            "transfer": {
              "enabled": true,
              "latency_ns": 3000,
              "bandwidth_bytes_per_s": 50000000000.0,
              "efficiency": 0.85
            }
          },
          "cost_model": {
            "prefill": {
              "base_latency_ns": 10,
              "attention_compute_ns_per_token": 3,
              "ffn_or_expert_compute_ns_per_token": 5,
              "tp_collective_ns_per_token": 7,
              "batch_efficiency": 0.5
            },
            "decode": {
              "base_latency_ns": 20,
              "attention_compute_ns_per_token": 2,
              "ffn_or_expert_compute_ns_per_token": 4,
              "tp_collective_ns_per_token": 6,
              "pp_activation_ns_per_token": 8,
              "ep_dispatch_ns_per_token": 10,
              "dp_attention_sync_ns_per_token": 12,
              "batch_efficiency": 0.75,
              "interference_factor": 1.25
            }
          },
          "requests": [
            {"request_id": 0, "arrival_time_ns": 0, "prompt_tokens": 16, "output_tokens": 4}
          ]
        }
        )json",
        "<topology>");

    expect_true(config.topology.has_value(), "topology should parse");
    expect_true(
        config.topology->deployment ==
            ServingTopologyDeployment::PrefillDecodeDisaggregated,
        "topology deployment should parse");
    expect_equal(config.topology->prefill_layout->tp_degree, 2,
                 "prefill layout TP should parse");
    expect_equal(config.topology->decode_layout->pp_degree, 2,
                 "decode layout PP should parse");
    expect_equal(config.topology->decode_layout->ep_degree, 8,
                 "decode layout EP should parse");
    expect_equal(config.topology->decode_layout->dp_attention_degree, 4,
                 "decode layout DP-attention should parse");
    expect_equal(config.cost_model.prefill_compute_ns_per_token, 15,
                 "legacy prefill compute field should translate from components");
    expect_equal(config.cost_model.decode_compute_ns_per_token, 42,
                 "legacy decode compute field should translate from components");
}

void test_topology_aware_cost_breakdown() {
    auto config = ServingConfig::load_from_json_text(
        R"json(
        {
          "runtime": {"architecture": "colocated"},
          "topology": {
            "colocated_layout": {
              "name": "base-layout",
              "tp_degree": 1,
              "pp_degree": 1,
              "ep_degree": 1,
              "dp_attention_degree": 1,
              "dp_replica_count": 2
            }
          },
          "model": {
            "name": "dense-toy",
            "kind": "dense",
            "num_layers": 32,
            "hidden_size": 4096,
            "ffn_hidden_size": 14336,
            "attention_heads": 32,
            "kv_heads": 8,
            "head_dim": 128,
            "bytes_per_activation": 2,
            "bytes_per_kv_element": 2
          },
          "baseline": {
            "prefill_base_latency_ns": 0,
            "prefill_compute_ns_per_token": 1000,
            "decode_base_latency_ns": 0,
            "decode_compute_ns_per_token": 500,
            "prefill_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0},
            "decode_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0}
          },
          "requests": [
            {"request_id": 0, "arrival_time_ns": 0, "prompt_tokens": 64, "output_tokens": 8}
          ]
        }
        )json",
        "<topology-aware>");

    config.cluster = ClusterSpec{
        {DeviceSpec{"gpu", DeviceType::GPU, 8,
                    80ULL * 1024ULL * 1024ULL * 1024ULL, 900e12, 3.0e12,
                    700.0, 120.0}},
        4ULL * 1024ULL * 1024ULL * 1024ULL,
        std::nullopt,
    };
    config.interconnects = {
        InterconnectSpec{"gpu-fabric", DeviceType::GPU, DeviceType::GPU,
                         9.0e11, 800, true, 0.85},
    };

    ServingCostModel cost_model(config, 1.0, 1.0);

    ServingBatch base_batch;
    base_batch.items.push_back(ServingBatchItem{0, 64});
    base_batch.sequence_length_hint = 512;
    base_batch.layout = ParallelismLayoutSpec{"base", 1, 1, 1, 1, 2};

    auto tp_batch = base_batch;
    tp_batch.layout = ParallelismLayoutSpec{"tp", 4, 1, 1, 1, 2};
    auto pp_batch = base_batch;
    pp_batch.layout = ParallelismLayoutSpec{"pp", 1, 4, 1, 1, 2};

    auto moe_batch = base_batch;
    config.model->kind = "moe";
    config.model->experts_per_layer = 8;
    config.model->active_experts_per_token = 2;
    config.model->expert_hidden_size = 14336;
    ServingCostModel moe_cost_model(config, 1.0, 1.0);
    moe_batch.layout = ParallelismLayoutSpec{"moe", 1, 1, 4, 4, 2};

    const auto tp_breakdown = cost_model.estimate_prefill_breakdown(tp_batch);
    const auto pp_breakdown = cost_model.estimate_prefill_breakdown(pp_batch);
    const auto moe_breakdown = moe_cost_model.estimate_decode_breakdown(moe_batch);

    expect_true(tp_breakdown.tp_collective_ns > 0,
                "topology-aware TP should add collective cost");
    expect_true(pp_breakdown.pp_activation_ns > 0,
                "topology-aware PP should add activation/bubble cost");
    expect_true(moe_breakdown.ep_dispatch_ns > 0,
                "MoE EP should add dispatch cost");
    expect_true(moe_breakdown.dp_attention_sync_ns > 0,
                "DP-attention should add sync cost");

    ServingTopology topology(config, 8);
    expect_equal(topology.replica_count(), 2,
                 "replica count should follow topology layout");
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

    bool saw_invalid_pd = false;
    try {
        (void)ServingConfig::load_from_json_text(
            R"json(
            {
              "runtime": {"architecture": "pd_disaggregated"},
              "baseline": {
                "prefill_base_latency_ns": 0,
                "prefill_compute_ns_per_token": 1,
                "decode_base_latency_ns": 0,
                "decode_compute_ns_per_token": 1,
                "prefill_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0},
                "decode_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0}
              },
              "requests": [
                {"request_id": 0, "arrival_time_ns": 0, "prompt_tokens": 1, "output_tokens": 1}
              ]
            }
            )json",
            "<invalid-pd>");
    } catch (const std::runtime_error&) {
        saw_invalid_pd = true;
    }
    expect_true(saw_invalid_pd,
                "pd_disaggregated configs should require a pd section");

    bool saw_invalid_chunk = false;
    try {
        (void)ServingConfig::load_from_json_text(
            R"json(
            {
              "runtime": {"architecture": "colocated_chunked"},
              "scheduler": {"chunked_prefill_size": 0},
              "baseline": {
                "prefill_base_latency_ns": 0,
                "prefill_compute_ns_per_token": 1,
                "decode_base_latency_ns": 0,
                "decode_compute_ns_per_token": 1,
                "prefill_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0},
                "decode_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0}
              },
              "requests": [
                {"request_id": 0, "arrival_time_ns": 0, "prompt_tokens": 1, "output_tokens": 1}
              ]
            }
            )json",
            "<invalid-chunk>");
    } catch (const std::runtime_error&) {
        saw_invalid_chunk = true;
    }
    expect_true(saw_invalid_chunk,
                "chunked colocated configs should require a positive chunk size");

    bool saw_invalid_slo = false;
    try {
        (void)ServingConfig::load_from_json_text(
            R"json(
            {
              "slo": {"ttft_ns": 0, "tpot_ns": 0.0},
              "baseline": {
                "prefill_base_latency_ns": 0,
                "prefill_compute_ns_per_token": 1,
                "decode_base_latency_ns": 0,
                "decode_compute_ns_per_token": 1,
                "prefill_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0},
                "decode_collective": {"enabled": false, "type": "all-reduce", "size_bytes": 0}
              },
              "requests": [
                {"request_id": 0, "arrival_time_ns": 0, "prompt_tokens": 1, "output_tokens": 1}
              ]
            }
            )json",
            "<invalid-slo>");
    } catch (const std::runtime_error&) {
        saw_invalid_slo = true;
    }
    expect_true(saw_invalid_slo,
                "SLOs with no positive thresholds should be rejected");

    bool saw_invalid_curve = false;
    try {
        (void)ServingConfig::load_from_json_text(
            R"json(
            {
              "cost_model": {
                "prefill_base_latency_ns": 0,
                "prefill_ns_per_token": 1,
                "decode_base_latency_ns": 0,
                "decode_ns_per_token": 1,
                "decode_step_latency_curve": {
                  "enabled": true,
                  "points": [
                    {"request_rate_per_second": 4.0, "decode_step_latency_ns": 100},
                    {"request_rate_per_second": 2.0, "decode_step_latency_ns": 200}
                  ]
                }
              },
              "requests": [
                {"request_id": 0, "arrival_time_ns": 0, "prompt_tokens": 1, "output_tokens": 1}
              ]
            }
            )json",
            "<invalid-curve>");
    } catch (const std::runtime_error&) {
        saw_invalid_curve = true;
    }
    expect_true(saw_invalid_curve,
                "decode step latency curve points should be strictly increasing");

    bool saw_invalid_first_token_curve = false;
    try {
        (void)ServingConfig::load_from_json_text(
            R"json(
            {
              "cost_model": {
                "prefill_base_latency_ns": 0,
                "prefill_ns_per_token": 1,
                "decode_base_latency_ns": 0,
                "decode_ns_per_token": 1,
                "first_token_backpressure_curve": {
                  "enabled": true,
                  "points": [
                    {
                      "request_rate_per_second": 2.0,
                      "base_latency_ns": 100,
                      "knee_request_index": 1.0,
                      "latency_ns_per_request_after_knee": 2.0
                    },
                    {
                      "request_rate_per_second": 4.0,
                      "base_latency_ns": 200,
                      "knee_request_index": -1.0,
                      "latency_ns_per_request_after_knee": 4.0
                    }
                  ]
                }
              },
              "requests": [
                {"request_id": 0, "arrival_time_ns": 0, "prompt_tokens": 1, "output_tokens": 1}
              ]
            }
            )json",
            "<invalid-first-token-curve>");
    } catch (const std::runtime_error&) {
        saw_invalid_first_token_curve = true;
    }
    expect_true(
        saw_invalid_first_token_curve,
        "first token backpressure curve should reject negative knee values");
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
        test_explicit_legacy_config();
        test_runtime_scheduler_and_slo_config();
        test_calibration_fidelity_config();
        test_decode_step_latency_curve();
        test_generated_trace_determinism();
        test_goodput_math();
        test_transfer_model_math();
        test_topology_and_component_config();
        test_topology_aware_cost_breakdown();
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
