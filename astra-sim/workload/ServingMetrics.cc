#include "astra-sim/workload/ServingMetrics.hh"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <json/json.hpp>
#include <map>
#include <sstream>
#include <stdexcept>

#include "astra-sim/common/Logging.hh"
#include "astra-sim/workload/ServingTypes.hh"
#include "astra-sim/workload/ServingUtils.hh"

#ifndef ASTRA_SIM_GIT_COMMIT
#define ASTRA_SIM_GIT_COMMIT "unknown"
#endif

#ifndef ASTRA_SIM_VERSION
#define ASTRA_SIM_VERSION "serving-week2-v1"
#endif

using json = nlohmann::json;

namespace AstraSim {

namespace {

[[noreturn]] void serving_metrics_error(const std::string& message) {
    LoggerFactory::get_logger("serving")->critical(message);
    throw std::runtime_error(message);
}

json stats_to_json(const ServingMetricStats& stats) {
    return json{
        {"count", stats.count},
        {"mean", stats.mean},
        {"p50", stats.p50},
        {"p90", stats.p90},
        {"p99", stats.p99},
        {"max", stats.max},
    };
}

json breakdown_to_json(const ServingStageBreakdown& breakdown) {
    return json{
        {"base_latency_ns", breakdown.base_latency_ns},
        {"attention_compute_ns", breakdown.attention_compute_ns},
        {"ffn_or_expert_compute_ns", breakdown.ffn_or_expert_compute_ns},
        {"tp_collective_ns", breakdown.tp_collective_ns},
        {"pp_activation_ns", breakdown.pp_activation_ns},
        {"ep_dispatch_ns", breakdown.ep_dispatch_ns},
        {"dp_attention_sync_ns", breakdown.dp_attention_sync_ns},
        {"pd_kv_transfer_ns", breakdown.pd_kv_transfer_ns},
        {"total_ns", breakdown.total_ns()},
    };
}

std::string make_timestamp_utc() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc_tm{};
    gmtime_r(&time, &utc_tm);

    std::ostringstream stream;
    stream << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

bool has_output_path(const std::string& path) {
    return !path.empty() && path != "empty";
}

bool use_enhanced_reporting(const ServingConfig& config) {
    return config.cluster.has_value() || config.cost_model.topology_aware_enabled;
}

std::vector<ServingRequestMetrics> sort_metrics(
    const std::vector<ServingRequestMetrics>& metrics) {
    auto sorted_metrics = metrics;
    std::sort(sorted_metrics.begin(), sorted_metrics.end(),
              [](const ServingRequestMetrics& lhs,
                 const ServingRequestMetrics& rhs) {
                  return lhs.original_index < rhs.original_index;
              });
    return sorted_metrics;
}

void collect_metric_values(const std::vector<ServingRequestMetrics>& metrics,
                           std::vector<double>* queue_delay_values,
                           std::vector<double>* prefill_queue_delay_values,
                           std::vector<double>* decode_queue_delay_values,
                           std::vector<double>* transfer_duration_values,
                           std::vector<double>* ttft_values,
                           std::vector<double>* tpot_values,
                           std::vector<double>* e2e_values) {
    queue_delay_values->clear();
    prefill_queue_delay_values->clear();
    decode_queue_delay_values->clear();
    transfer_duration_values->clear();
    ttft_values->clear();
    tpot_values->clear();
    e2e_values->clear();

    for (const auto& metric : metrics) {
        queue_delay_values->push_back(
            static_cast<double>(metric.queue_delay_ns));
        prefill_queue_delay_values->push_back(
            static_cast<double>(metric.prefill_queue_delay_ns));
        decode_queue_delay_values->push_back(
            static_cast<double>(metric.decode_queue_delay_ns));
        transfer_duration_values->push_back(
            static_cast<double>(metric.transfer_duration_ns));
        ttft_values->push_back(static_cast<double>(metric.ttft_ns));
        tpot_values->push_back(metric.tpot_ns);
        e2e_values->push_back(static_cast<double>(metric.e2e_ns));
    }
}

}  // namespace

void emit_serving_request_log(const ServingRequestMetrics& metrics) {
    std::ostringstream message;
    message << std::fixed << std::setprecision(2)
            << "REQUEST request_id=" << metrics.request_id
            << " arrival_time_ns=" << metrics.arrival_time_ns
            << " prompt_tokens=" << metrics.prompt_tokens
            << " output_tokens=" << metrics.output_tokens
            << " service_start_ns=" << metrics.service_start_ns
            << " prefill_end_time_ns=" << metrics.prefill_end_time_ns
            << " first_token_time_ns=" << metrics.first_token_time_ns
            << " finish_time_ns=" << metrics.finish_time_ns
            << " queue_delay_ns=" << metrics.queue_delay_ns
            << " prefill_duration_ns=" << metrics.prefill_duration_ns
            << " decode_duration_ns=" << metrics.decode_duration_ns
            << " ttft_ns=" << metrics.ttft_ns
            << " tpot_ns=" << metrics.tpot_ns
            << " e2e_ns=" << metrics.e2e_ns;
    LoggerFactory::get_logger("serving")->info(message.str());
}

void emit_serving_summary_log(const std::string& metric_name,
                              const ServingMetricStats& stats) {
    std::ostringstream message;
    message << std::fixed << std::setprecision(2)
            << "SUMMARY metric=" << metric_name
            << " count=" << stats.count
            << " avg_ns=" << stats.mean
            << " p50_ns=" << stats.p50
            << " p90_ns=" << stats.p90
            << " p99_ns=" << stats.p99
            << " max_ns=" << stats.max;
    LoggerFactory::get_logger("serving")->info(message.str());
}

void emit_serving_throughput_log(size_t total_requests,
                                 Tick makespan_ns,
                                 uint64_t total_output_tokens) {
    std::ostringstream message;
    message << std::fixed << std::setprecision(6)
            << "THROUGHPUT total_requests=" << total_requests
            << " total_output_tokens=" << total_output_tokens
            << " makespan_ns=" << makespan_ns
            << " request_throughput_reqs_per_sec="
            << calculate_throughput_per_second(total_requests, makespan_ns)
            << " output_token_throughput_tokens_per_sec="
            << calculate_throughput_per_second(total_output_tokens,
                                               makespan_ns);
    LoggerFactory::get_logger("serving")->info(message.str());
}

void write_serving_metrics_csv(const std::string& path,
                               const ServingConfig& config,
                               const std::vector<ServingRequestMetrics>& metrics) {
    if (!has_output_path(path)) {
        return;
    }
    std::ofstream output(path);
    if (!output.is_open()) {
        serving_metrics_error("Unable to open request metrics output file: " +
                              path);
    }

    const bool use_enhanced = use_enhanced_reporting(config);

    if (!use_enhanced) {
        output << "request_id,arrival_time_ns,prompt_tokens,output_tokens,"
                  "architecture,service_start_ns,prefill_queue_enter_ns,"
                  "prefill_start_ns,prefill_end_time_ns,transfer_queue_enter_ns,"
                  "transfer_start_ns,transfer_end_ns,decode_queue_enter_ns,"
                  "decode_start_ns,first_token_time_ns,finish_time_ns,"
                  "queue_delay_ns,prefill_queue_delay_ns,decode_queue_delay_ns,"
                  "transfer_queue_delay_ns,prefill_duration_ns,"
                  "transfer_duration_ns,decode_duration_ns,prefill_service_ns,"
                  "transfer_service_ns,decode_service_ns,prefill_stage_wait_ns,"
                  "transfer_stage_wait_ns,decode_stage_wait_ns,"
                  "total_prefill_queue_wait_ns,total_transfer_queue_wait_ns,"
                  "total_decode_queue_wait_ns,prefill_chunk_count,"
                  "transfer_handoff_count,max_prefill_chunk_tokens,"
                  "max_transfer_chunk_tokens,ttft_ns,tpot_ns,e2e_ns,"
                  "goodput_slo_configured,ttft_slo_pass,tpot_slo_pass,"
                  "e2e_slo_pass,request_good,goodput_fail_reason\n";
    } else {
        output << "request_id,arrival_time_ns,prompt_tokens,output_tokens,"
                  "architecture,replica_id,prefill_layout_name,decode_layout_name,"
                  "service_start_ns,prefill_queue_enter_ns,"
                  "prefill_start_ns,prefill_end_time_ns,transfer_queue_enter_ns,"
                  "transfer_start_ns,transfer_end_ns,decode_queue_enter_ns,"
                  "decode_start_ns,first_token_time_ns,finish_time_ns,"
                  "queue_delay_ns,prefill_queue_delay_ns,decode_queue_delay_ns,"
                  "transfer_queue_delay_ns,prefill_duration_ns,"
                  "transfer_duration_ns,decode_duration_ns,prefill_service_ns,"
                  "transfer_service_ns,decode_service_ns,prefill_stage_wait_ns,"
                  "transfer_stage_wait_ns,decode_stage_wait_ns,"
                  "total_prefill_queue_wait_ns,total_transfer_queue_wait_ns,"
                  "total_decode_queue_wait_ns,prefill_chunk_count,"
                  "transfer_handoff_count,max_prefill_chunk_tokens,"
                  "max_transfer_chunk_tokens,ttft_ns,tpot_ns,kv_transfer_bytes,"
                  "kv_resident_bytes,"
                  "prefill_base_latency_ns,prefill_attention_compute_ns,prefill_ffn_or_expert_compute_ns,"
                  "prefill_tp_collective_ns,prefill_pp_activation_ns,prefill_ep_dispatch_ns,"
                  "prefill_dp_attention_sync_ns,prefill_pd_kv_transfer_ns,"
                  "decode_base_latency_ns,decode_attention_compute_ns,decode_ffn_or_expert_compute_ns,"
                  "decode_tp_collective_ns,decode_pp_activation_ns,decode_ep_dispatch_ns,"
                  "decode_dp_attention_sync_ns,decode_pd_kv_transfer_ns,"
                  "transfer_base_latency_ns,transfer_attention_compute_ns,transfer_ffn_or_expert_compute_ns,"
                  "transfer_tp_collective_ns,transfer_pp_activation_ns,transfer_ep_dispatch_ns,"
                  "transfer_dp_attention_sync_ns,transfer_pd_kv_transfer_ns,"
                  "e2e_ns,goodput_slo_configured,ttft_slo_pass,tpot_slo_pass,"
                  "e2e_slo_pass,request_good,goodput_fail_reason\n";
    }

    output << std::fixed << std::setprecision(2);
    for (const auto& metric : sort_metrics(metrics)) {
        if (!use_enhanced) {
            output << metric.request_id << "," << metric.arrival_time_ns << ","
                   << metric.prompt_tokens << "," << metric.output_tokens
                   << "," << metric.architecture << ","
                   << metric.service_start_ns << ","
                   << metric.prefill_queue_enter_ns << ","
                   << metric.prefill_start_ns << ","
                   << metric.prefill_end_time_ns << ","
                   << metric.transfer_queue_enter_ns << ","
                   << metric.transfer_start_ns << ","
                   << metric.transfer_end_ns << ","
                   << metric.decode_queue_enter_ns << ","
                   << metric.decode_start_ns << ","
                   << metric.first_token_time_ns << ","
                   << metric.finish_time_ns << "," << metric.queue_delay_ns
                   << "," << metric.prefill_queue_delay_ns << ","
                   << metric.decode_queue_delay_ns << ","
                   << metric.transfer_queue_delay_ns << ","
                   << metric.prefill_duration_ns << ","
                   << metric.transfer_duration_ns << ","
                   << metric.decode_duration_ns << ","
                   << metric.prefill_service_ns << ","
                   << metric.transfer_service_ns << ","
                   << metric.decode_service_ns << ","
                   << metric.prefill_stage_wait_ns << ","
                   << metric.transfer_stage_wait_ns << ","
                   << metric.decode_stage_wait_ns << ","
                   << metric.total_prefill_queue_wait_ns << ","
                   << metric.total_transfer_queue_wait_ns << ","
                   << metric.total_decode_queue_wait_ns << ","
                   << metric.prefill_chunk_count << ","
                   << metric.transfer_handoff_count << ","
                   << metric.max_prefill_chunk_tokens << ","
                   << metric.max_transfer_chunk_tokens << ","
                   << metric.ttft_ns
                   << "," << metric.tpot_ns << "," << metric.e2e_ns << ","
                   << (metric.goodput.slo_configured ? "true" : "false")
                   << "," << (metric.goodput.ttft_pass ? "true" : "false")
                   << "," << (metric.goodput.tpot_pass ? "true" : "false")
                   << "," << (metric.goodput.e2e_pass ? "true" : "false")
                   << "," << (metric.goodput.request_good ? "true" : "false")
                   << "," << metric.goodput.fail_reason << "\n";
        } else {
            output << metric.request_id << "," << metric.arrival_time_ns
                   << "," << metric.prompt_tokens << ","
                   << metric.output_tokens << "," << metric.architecture
                   << "," << metric.replica_id << ","
                   << metric.prefill_layout_name << ","
                   << metric.decode_layout_name << ","
                   << metric.service_start_ns << ","
                   << metric.prefill_queue_enter_ns << ","
                   << metric.prefill_start_ns << ","
                   << metric.prefill_end_time_ns << ","
                   << metric.transfer_queue_enter_ns << ","
                   << metric.transfer_start_ns << ","
                   << metric.transfer_end_ns << ","
                   << metric.decode_queue_enter_ns << ","
                   << metric.decode_start_ns << ","
                   << metric.first_token_time_ns << ","
                   << metric.finish_time_ns << "," << metric.queue_delay_ns
                   << "," << metric.prefill_queue_delay_ns << ","
                   << metric.decode_queue_delay_ns << ","
                   << metric.transfer_queue_delay_ns << ","
                   << metric.prefill_duration_ns << ","
                   << metric.transfer_duration_ns << ","
                   << metric.decode_duration_ns << ","
                   << metric.prefill_service_ns << ","
                   << metric.transfer_service_ns << ","
                   << metric.decode_service_ns << ","
                   << metric.prefill_stage_wait_ns << ","
                   << metric.transfer_stage_wait_ns << ","
                   << metric.decode_stage_wait_ns << ","
                   << metric.total_prefill_queue_wait_ns << ","
                   << metric.total_transfer_queue_wait_ns << ","
                   << metric.total_decode_queue_wait_ns << ","
                   << metric.prefill_chunk_count << ","
                   << metric.transfer_handoff_count << ","
                   << metric.max_prefill_chunk_tokens << ","
                   << metric.max_transfer_chunk_tokens << ","
                   << metric.ttft_ns
                   << "," << metric.tpot_ns << ","
                   << metric.kv_transfer_bytes << ","
                   << metric.kv_resident_bytes << ","
                   << metric.prefill_breakdown.base_latency_ns << ","
                   << metric.prefill_breakdown.attention_compute_ns << ","
                   << metric.prefill_breakdown.ffn_or_expert_compute_ns << ","
                   << metric.prefill_breakdown.tp_collective_ns << ","
                   << metric.prefill_breakdown.pp_activation_ns << ","
                   << metric.prefill_breakdown.ep_dispatch_ns << ","
                   << metric.prefill_breakdown.dp_attention_sync_ns << ","
                   << metric.prefill_breakdown.pd_kv_transfer_ns << ","
                   << metric.decode_breakdown.base_latency_ns << ","
                   << metric.decode_breakdown.attention_compute_ns << ","
                   << metric.decode_breakdown.ffn_or_expert_compute_ns << ","
                   << metric.decode_breakdown.tp_collective_ns << ","
                   << metric.decode_breakdown.pp_activation_ns << ","
                   << metric.decode_breakdown.ep_dispatch_ns << ","
                   << metric.decode_breakdown.dp_attention_sync_ns << ","
                   << metric.decode_breakdown.pd_kv_transfer_ns << ","
                   << metric.transfer_breakdown.base_latency_ns << ","
                   << metric.transfer_breakdown.attention_compute_ns << ","
                   << metric.transfer_breakdown.ffn_or_expert_compute_ns << ","
                   << metric.transfer_breakdown.tp_collective_ns << ","
                   << metric.transfer_breakdown.pp_activation_ns << ","
                   << metric.transfer_breakdown.ep_dispatch_ns << ","
                   << metric.transfer_breakdown.dp_attention_sync_ns << ","
                   << metric.transfer_breakdown.pd_kv_transfer_ns << ","
                   << metric.e2e_ns << ","
                   << (metric.goodput.slo_configured ? "true" : "false")
                   << "," << (metric.goodput.ttft_pass ? "true" : "false")
                   << "," << (metric.goodput.tpot_pass ? "true" : "false")
                   << "," << (metric.goodput.e2e_pass ? "true" : "false")
                   << "," << (metric.goodput.request_good ? "true" : "false")
                   << "," << metric.goodput.fail_reason << "\n";
        }
    }
}

void write_serving_summary_json(const std::string& path,
                                const ServingConfig& config,
                                const std::vector<ServingRequestMetrics>& metrics) {
    if (!has_output_path(path)) {
        return;
    }

    std::ofstream output(path);
    if (!output.is_open()) {
        serving_metrics_error("Unable to open request summary output file: " +
                              path);
    }

    std::vector<double> queue_delay_values;
    std::vector<double> prefill_queue_delay_values;
    std::vector<double> decode_queue_delay_values;
    std::vector<double> transfer_duration_values;
    std::vector<double> ttft_values;
    std::vector<double> tpot_values;
    std::vector<double> e2e_values;
    std::vector<double> prefill_service_values;
    std::vector<double> transfer_service_values;
    std::vector<double> decode_service_values;
    std::vector<double> prefill_stage_wait_values;
    std::vector<double> transfer_stage_wait_values;
    std::vector<double> decode_stage_wait_values;
    std::vector<double> total_prefill_queue_wait_values;
    std::vector<double> total_transfer_queue_wait_values;
    std::vector<double> total_decode_queue_wait_values;
    std::vector<double> prefill_chunk_count_values;
    std::vector<double> transfer_handoff_count_values;
    collect_metric_values(metrics, &queue_delay_values,
                          &prefill_queue_delay_values,
                          &decode_queue_delay_values, &transfer_duration_values,
                          &ttft_values, &tpot_values, &e2e_values);

    Tick simulation_start_time_ns = 0;
    Tick simulation_end_time_ns = 0;
    uint64_t total_prompt_tokens = 0;
    uint64_t total_output_tokens = 0;
    uint64_t total_kv_transfer_bytes = 0;
    uint64_t total_kv_resident_bytes = 0;
    uint64_t total_prefill_chunks = 0;
    uint64_t total_transfer_handoffs = 0;
    uint64_t good_requests = 0;
    uint64_t bad_requests = 0;
    uint64_t ttft_failures = 0;
    uint64_t tpot_failures = 0;
    uint64_t e2e_failures = 0;
    ServingStageBreakdown total_prefill_breakdown;
    ServingStageBreakdown total_decode_breakdown;
    ServingStageBreakdown total_transfer_breakdown;
    if (!metrics.empty()) {
        simulation_start_time_ns = metrics.front().arrival_time_ns;
        simulation_end_time_ns = metrics.front().finish_time_ns;
    }
    for (const auto& metric : metrics) {
        simulation_start_time_ns =
            std::min(simulation_start_time_ns, metric.arrival_time_ns);
        simulation_end_time_ns =
            std::max(simulation_end_time_ns, metric.finish_time_ns);
        total_prompt_tokens += metric.prompt_tokens;
        total_output_tokens += metric.output_tokens;
        total_kv_transfer_bytes += metric.kv_transfer_bytes;
        total_kv_resident_bytes += metric.kv_resident_bytes;
        total_prefill_chunks += metric.prefill_chunk_count;
        total_transfer_handoffs += metric.transfer_handoff_count;
        prefill_service_values.push_back(
            static_cast<double>(metric.prefill_service_ns));
        transfer_service_values.push_back(
            static_cast<double>(metric.transfer_service_ns));
        decode_service_values.push_back(
            static_cast<double>(metric.decode_service_ns));
        prefill_stage_wait_values.push_back(
            static_cast<double>(metric.prefill_stage_wait_ns));
        transfer_stage_wait_values.push_back(
            static_cast<double>(metric.transfer_stage_wait_ns));
        decode_stage_wait_values.push_back(
            static_cast<double>(metric.decode_stage_wait_ns));
        total_prefill_queue_wait_values.push_back(
            static_cast<double>(metric.total_prefill_queue_wait_ns));
        total_transfer_queue_wait_values.push_back(
            static_cast<double>(metric.total_transfer_queue_wait_ns));
        total_decode_queue_wait_values.push_back(
            static_cast<double>(metric.total_decode_queue_wait_ns));
        prefill_chunk_count_values.push_back(
            static_cast<double>(metric.prefill_chunk_count));
        transfer_handoff_count_values.push_back(
            static_cast<double>(metric.transfer_handoff_count));
        total_prefill_breakdown.base_latency_ns +=
            metric.prefill_breakdown.base_latency_ns;
        total_prefill_breakdown.attention_compute_ns +=
            metric.prefill_breakdown.attention_compute_ns;
        total_prefill_breakdown.ffn_or_expert_compute_ns +=
            metric.prefill_breakdown.ffn_or_expert_compute_ns;
        total_prefill_breakdown.tp_collective_ns +=
            metric.prefill_breakdown.tp_collective_ns;
        total_prefill_breakdown.pp_activation_ns +=
            metric.prefill_breakdown.pp_activation_ns;
        total_prefill_breakdown.ep_dispatch_ns +=
            metric.prefill_breakdown.ep_dispatch_ns;
        total_prefill_breakdown.dp_attention_sync_ns +=
            metric.prefill_breakdown.dp_attention_sync_ns;
        total_prefill_breakdown.pd_kv_transfer_ns +=
            metric.prefill_breakdown.pd_kv_transfer_ns;
        total_decode_breakdown.base_latency_ns +=
            metric.decode_breakdown.base_latency_ns;
        total_decode_breakdown.attention_compute_ns +=
            metric.decode_breakdown.attention_compute_ns;
        total_decode_breakdown.ffn_or_expert_compute_ns +=
            metric.decode_breakdown.ffn_or_expert_compute_ns;
        total_decode_breakdown.tp_collective_ns +=
            metric.decode_breakdown.tp_collective_ns;
        total_decode_breakdown.pp_activation_ns +=
            metric.decode_breakdown.pp_activation_ns;
        total_decode_breakdown.ep_dispatch_ns +=
            metric.decode_breakdown.ep_dispatch_ns;
        total_decode_breakdown.dp_attention_sync_ns +=
            metric.decode_breakdown.dp_attention_sync_ns;
        total_decode_breakdown.pd_kv_transfer_ns +=
            metric.decode_breakdown.pd_kv_transfer_ns;
        total_transfer_breakdown.base_latency_ns +=
            metric.transfer_breakdown.base_latency_ns;
        total_transfer_breakdown.attention_compute_ns +=
            metric.transfer_breakdown.attention_compute_ns;
        total_transfer_breakdown.ffn_or_expert_compute_ns +=
            metric.transfer_breakdown.ffn_or_expert_compute_ns;
        total_transfer_breakdown.tp_collective_ns +=
            metric.transfer_breakdown.tp_collective_ns;
        total_transfer_breakdown.pp_activation_ns +=
            metric.transfer_breakdown.pp_activation_ns;
        total_transfer_breakdown.ep_dispatch_ns +=
            metric.transfer_breakdown.ep_dispatch_ns;
        total_transfer_breakdown.dp_attention_sync_ns +=
            metric.transfer_breakdown.dp_attention_sync_ns;
        total_transfer_breakdown.pd_kv_transfer_ns +=
            metric.transfer_breakdown.pd_kv_transfer_ns;
        if (metric.goodput.request_good) {
            good_requests++;
        } else {
            bad_requests++;
        }
        if (!metric.goodput.ttft_pass) {
            ttft_failures++;
        }
        if (!metric.goodput.tpot_pass) {
            tpot_failures++;
        }
        if (!metric.goodput.e2e_pass) {
            e2e_failures++;
        }
    }

    const auto makespan_ns = simulation_end_time_ns - simulation_start_time_ns;
    const auto num_requests = metrics.size();
    json summary;
    if (!use_enhanced_reporting(config)) {
        summary = json{
            {"architecture", to_string(config.runtime.architecture)},
            {"num_requests", num_requests},
            {"total_prompt_tokens", total_prompt_tokens},
            {"total_output_tokens", total_output_tokens},
            {"simulation_start_time_ns", simulation_start_time_ns},
            {"simulation_end_time_ns", simulation_end_time_ns},
            {"makespan_ns", makespan_ns},
            {"request_throughput_reqs_per_sec",
             calculate_throughput_per_second(num_requests, makespan_ns)},
            {"output_token_throughput_tokens_per_sec",
             calculate_throughput_per_second(total_output_tokens, makespan_ns)},
            {"queue_delay_ns",
             stats_to_json(summarize_serving_metric(queue_delay_values))},
            {"prefill_queue_delay_ns",
             stats_to_json(summarize_serving_metric(prefill_queue_delay_values))},
            {"decode_queue_delay_ns",
             stats_to_json(summarize_serving_metric(decode_queue_delay_values))},
            {"transfer_duration_ns",
             stats_to_json(summarize_serving_metric(transfer_duration_values))},
            {"prefill_service_ns",
             stats_to_json(summarize_serving_metric(prefill_service_values))},
            {"transfer_service_ns",
             stats_to_json(summarize_serving_metric(transfer_service_values))},
            {"decode_service_ns",
             stats_to_json(summarize_serving_metric(decode_service_values))},
            {"prefill_stage_wait_ns",
             stats_to_json(summarize_serving_metric(prefill_stage_wait_values))},
            {"transfer_stage_wait_ns",
             stats_to_json(summarize_serving_metric(
                 transfer_stage_wait_values))},
            {"decode_stage_wait_ns",
             stats_to_json(summarize_serving_metric(decode_stage_wait_values))},
            {"total_prefill_queue_wait_ns",
             stats_to_json(summarize_serving_metric(
                 total_prefill_queue_wait_values))},
            {"total_transfer_queue_wait_ns",
             stats_to_json(summarize_serving_metric(
                 total_transfer_queue_wait_values))},
            {"total_decode_queue_wait_ns",
             stats_to_json(summarize_serving_metric(
                 total_decode_queue_wait_values))},
            {"prefill_chunk_count",
             stats_to_json(summarize_serving_metric(prefill_chunk_count_values))},
            {"transfer_handoff_count",
             stats_to_json(
                 summarize_serving_metric(transfer_handoff_count_values))},
            {"ttft_ns", stats_to_json(summarize_serving_metric(ttft_values))},
            {"tpot_ns", stats_to_json(summarize_serving_metric(tpot_values))},
            {"e2e_ns", stats_to_json(summarize_serving_metric(e2e_values))},
            {"total_prefill_chunks", total_prefill_chunks},
            {"total_transfer_handoffs", total_transfer_handoffs},
            {"slo",
             json{{"configured", config.slo.enabled},
                  {"ttft_ns", config.slo.ttft_ns},
                  {"tpot_ns", config.slo.tpot_ns},
                  {"e2e_ns", config.slo.e2e_ns.has_value()
                                 ? json(*config.slo.e2e_ns)
                                 : json(nullptr)}}},
            {"goodput",
             json{{"good_requests", good_requests},
                  {"bad_requests", bad_requests},
                  {"slo_attainment_fraction",
                   num_requests == 0
                       ? 0.0
                       : static_cast<double>(good_requests) /
                             static_cast<double>(num_requests)},
                  {"goodput_reqs_per_sec",
                   calculate_throughput_per_second(good_requests, makespan_ns)},
                  {"failure_reasons",
                   json{{"ttft", ttft_failures},
                        {"tpot", tpot_failures},
                        {"e2e", e2e_failures}}}}}
        };
    } else {
        summary = json{
            {"architecture", to_string(config.runtime.architecture)},
            {"num_requests", num_requests},
            {"total_prompt_tokens", total_prompt_tokens},
            {"total_output_tokens", total_output_tokens},
            {"total_kv_transfer_bytes", total_kv_transfer_bytes},
            {"total_kv_resident_bytes", total_kv_resident_bytes},
            {"simulation_start_time_ns", simulation_start_time_ns},
            {"simulation_end_time_ns", simulation_end_time_ns},
            {"makespan_ns", makespan_ns},
            {"request_throughput_reqs_per_sec",
             calculate_throughput_per_second(num_requests, makespan_ns)},
            {"output_token_throughput_tokens_per_sec",
             calculate_throughput_per_second(total_output_tokens, makespan_ns)},
            {"queue_delay_ns",
             stats_to_json(summarize_serving_metric(queue_delay_values))},
            {"prefill_queue_delay_ns",
             stats_to_json(summarize_serving_metric(prefill_queue_delay_values))},
            {"decode_queue_delay_ns",
             stats_to_json(summarize_serving_metric(decode_queue_delay_values))},
            {"transfer_duration_ns",
             stats_to_json(summarize_serving_metric(transfer_duration_values))},
            {"prefill_service_ns",
             stats_to_json(summarize_serving_metric(prefill_service_values))},
            {"transfer_service_ns",
             stats_to_json(summarize_serving_metric(transfer_service_values))},
            {"decode_service_ns",
             stats_to_json(summarize_serving_metric(decode_service_values))},
            {"prefill_stage_wait_ns",
             stats_to_json(summarize_serving_metric(prefill_stage_wait_values))},
            {"transfer_stage_wait_ns",
             stats_to_json(summarize_serving_metric(
                 transfer_stage_wait_values))},
            {"decode_stage_wait_ns",
             stats_to_json(summarize_serving_metric(decode_stage_wait_values))},
            {"total_prefill_queue_wait_ns",
             stats_to_json(summarize_serving_metric(
                 total_prefill_queue_wait_values))},
            {"total_transfer_queue_wait_ns",
             stats_to_json(summarize_serving_metric(
                 total_transfer_queue_wait_values))},
            {"total_decode_queue_wait_ns",
             stats_to_json(summarize_serving_metric(
                 total_decode_queue_wait_values))},
            {"prefill_chunk_count",
             stats_to_json(summarize_serving_metric(prefill_chunk_count_values))},
            {"transfer_handoff_count",
             stats_to_json(
                 summarize_serving_metric(transfer_handoff_count_values))},
            {"ttft_ns", stats_to_json(summarize_serving_metric(ttft_values))},
            {"tpot_ns", stats_to_json(summarize_serving_metric(tpot_values))},
            {"e2e_ns", stats_to_json(summarize_serving_metric(e2e_values))},
            {"total_prefill_chunks", total_prefill_chunks},
            {"total_transfer_handoffs", total_transfer_handoffs},
            {"prefill_breakdown", breakdown_to_json(total_prefill_breakdown)},
            {"decode_breakdown", breakdown_to_json(total_decode_breakdown)},
            {"transfer_breakdown", breakdown_to_json(total_transfer_breakdown)},
            {"slo",
             json{{"configured", config.slo.enabled},
                  {"ttft_ns", config.slo.ttft_ns},
                  {"tpot_ns", config.slo.tpot_ns},
                  {"e2e_ns", config.slo.e2e_ns.has_value()
                                 ? json(*config.slo.e2e_ns)
                                 : json(nullptr)}}},
            {"goodput",
             json{{"good_requests", good_requests},
                  {"bad_requests", bad_requests},
                  {"slo_attainment_fraction",
                   num_requests == 0
                       ? 0.0
                       : static_cast<double>(good_requests) /
                             static_cast<double>(num_requests)},
                  {"goodput_reqs_per_sec",
                   calculate_throughput_per_second(good_requests, makespan_ns)},
                  {"failure_reasons",
                   json{{"ttft", ttft_failures},
                        {"tpot", tpot_failures},
                        {"e2e", e2e_failures}}}}}
        };
    }

    output << summary.dump(2) << "\n";
}

void write_serving_run_metadata_json(const std::string& path,
                                     const ServingConfig& config,
                                     const ServingOutputPaths& outputs,
                                     const std::string& binary_name) {
    if (!has_output_path(path)) {
        return;
    }

    std::ofstream output(path);
    if (!output.is_open()) {
        serving_metrics_error(
            "Unable to open request run metadata output file: " + path);
    }

    const json seed_json = config.runtime.seed.has_value()
                               ? json(*config.runtime.seed)
                               : (config.trace_seed.has_value()
                                      ? json(*config.trace_seed)
                                      : json(nullptr));

    json metadata{
        {"timestamp_utc", make_timestamp_utc()},
        {"git_commit", ASTRA_SIM_GIT_COMMIT},
        {"config_path", outputs.request_configuration_path},
        {"seed", seed_json},
        {"trace_seed", config.trace_seed ? json(*config.trace_seed) : json(nullptr)},
        {"runtime_seed",
         config.runtime.seed ? json(*config.runtime.seed) : json(nullptr)},
        {"binary", binary_name},
        {"architecture", to_string(config.runtime.architecture)},
        {"simulator_version", ASTRA_SIM_VERSION},
    };
    if (use_enhanced_reporting(config)) {
        metadata["topology_deployment"] =
            config.topology.has_value()
                ? json(to_string(config.topology->deployment))
                : json(nullptr);
    }

    output << metadata.dump(2) << "\n";
}

void write_serving_event_trace_csv(
    const std::string& path,
    const ServingConfig& config,
    const std::vector<ServingEventTraceRecord>& records) {
    if (!has_output_path(path)) {
        return;
    }

    std::ofstream output(path);
    if (!output.is_open()) {
        serving_metrics_error("Unable to open serving event trace output: " +
                              path);
    }

    const bool use_enhanced = use_enhanced_reporting(config);
    if (!use_enhanced) {
        output << "time_ns,event,batch_id,worker_id,stage,request_ids,total_tokens,"
                  "request_count,duration_ns,include_base_latency,"
                  "running_request_count,admission_queue_depth,"
                  "prefill_queue_depth,transfer_queue_depth,decode_queue_depth,"
                  "inflight_transfer_count\n";
    } else {
        output << "time_ns,event,batch_id,worker_id,worker_group_id,replica_id,stage,"
                  "layout_name,request_ids,total_tokens,request_count,"
                  "duration_ns,include_base_latency,running_request_count,"
                  "admission_queue_depth,prefill_queue_depth,"
                  "transfer_queue_depth,decode_queue_depth,"
                  "inflight_transfer_count\n";
    }
    for (const auto& record : records) {
        if (!use_enhanced) {
            output << record.time_ns << "," << record.event << ","
                   << record.batch_id << "," << record.worker_id << ","
                   << record.stage << "," << record.request_ids << ","
                   << record.total_tokens << "," << record.request_count << ","
                   << record.duration_ns << ","
                   << (record.include_base_latency ? "true" : "false") << ","
                   << record.running_request_count << ","
                   << record.admission_queue_depth << ","
                   << record.prefill_queue_depth << ","
                   << record.transfer_queue_depth << ","
                   << record.decode_queue_depth << ","
                   << record.inflight_transfer_count
                   << "\n";
        } else {
            output << record.time_ns << "," << record.event << ","
                   << record.batch_id << "," << record.worker_id << ","
                   << record.worker_group_id << "," << record.replica_id
                   << "," << record.stage << "," << record.layout_name << ","
                   << record.request_ids << "," << record.total_tokens << ","
                   << record.request_count << "," << record.duration_ns << ","
                   << (record.include_base_latency ? "true" : "false") << ","
                   << record.running_request_count << ","
                   << record.admission_queue_depth << ","
                   << record.prefill_queue_depth << ","
                   << record.transfer_queue_depth << ","
                   << record.decode_queue_depth << ","
                   << record.inflight_transfer_count << "\n";
        }
    }
}

void write_serving_stage_metrics_csv(
    const std::string& path,
    const ServingConfig& config,
    const std::vector<ServingStageMetricsRecord>& records) {
    if (!has_output_path(path)) {
        return;
    }

    std::ofstream output(path);
    if (!output.is_open()) {
        serving_metrics_error("Unable to open serving stage metrics output: " +
                              path);
    }

    const bool use_enhanced = use_enhanced_reporting(config);
    if (!use_enhanced) {
        output << "batch_id,worker_id,stage,request_ids,request_count,"
                  "total_tokens,include_base_latency,scheduled_at_ns,"
                  "completed_at_ns,duration_ns,running_request_count,"
                  "admission_queue_depth,prefill_queue_depth,"
                  "transfer_queue_depth,decode_queue_depth,"
                  "inflight_transfer_count\n";
    } else {
        output << "batch_id,worker_id,worker_group_id,replica_id,stage,"
                  "layout_name,request_ids,request_count,total_tokens,"
                  "include_base_latency,scheduled_at_ns,completed_at_ns,"
                  "duration_ns,running_request_count,admission_queue_depth,"
                  "prefill_queue_depth,transfer_queue_depth,"
                  "decode_queue_depth,inflight_transfer_count\n";
    }

    for (const auto& record : records) {
        if (!use_enhanced) {
            output << record.batch_id << "," << record.worker_id << ","
                   << record.stage << "," << record.request_ids << ","
                   << record.request_count << "," << record.total_tokens << ","
                   << (record.include_base_latency ? "true" : "false") << ","
                   << record.scheduled_at_ns << "," << record.completed_at_ns
                   << "," << record.duration_ns << ","
                   << record.running_request_count << ","
                   << record.admission_queue_depth << ","
                   << record.prefill_queue_depth << ","
                   << record.transfer_queue_depth << ","
                   << record.decode_queue_depth << ","
                   << record.inflight_transfer_count << "\n";
        } else {
            output << record.batch_id << "," << record.worker_id << ","
                   << record.worker_group_id << "," << record.replica_id << ","
                   << record.stage << "," << record.layout_name << ","
                   << record.request_ids << "," << record.request_count << ","
                   << record.total_tokens << ","
                   << (record.include_base_latency ? "true" : "false") << ","
                   << record.scheduled_at_ns << "," << record.completed_at_ns
                   << "," << record.duration_ns << ","
                   << record.running_request_count << ","
                   << record.admission_queue_depth << ","
                   << record.prefill_queue_depth << ","
                   << record.transfer_queue_depth << ","
                   << record.decode_queue_depth << ","
                   << record.inflight_transfer_count << "\n";
        }
    }
}

}  // namespace AstraSim
