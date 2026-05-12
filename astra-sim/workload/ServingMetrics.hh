#ifndef __SERVING_METRICS_HH__
#define __SERVING_METRICS_HH__

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "astra-sim/system/Common.hh"
#include "astra-sim/workload/ServingConfig.hh"

namespace AstraSim {

struct ServingGoodputResult {
    bool slo_configured = false;
    bool ttft_pass = true;
    bool tpot_pass = true;
    bool e2e_pass = true;
    bool request_good = true;
    std::string fail_reason;
};

struct ServingMetricStats {
    size_t count = 0;
    double mean = 0.0;
    double p50 = 0.0;
    double p90 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
};

struct ServingRequestMetrics {
    size_t original_index = 0;
    uint64_t request_id = 0;
    Tick arrival_time_ns = 0;
    uint64_t prompt_tokens = 0;
    uint64_t output_tokens = 0;
    std::string architecture;
    size_t replica_id = 0;
    std::string prefill_layout_name;
    std::string decode_layout_name;
    Tick service_start_ns = 0;
    Tick prefill_queue_enter_ns = 0;
    Tick prefill_start_ns = 0;
    Tick prefill_end_time_ns = 0;
    Tick transfer_queue_enter_ns = 0;
    Tick transfer_start_ns = 0;
    Tick transfer_end_ns = 0;
    Tick decode_queue_enter_ns = 0;
    Tick decode_start_ns = 0;
    Tick first_token_time_ns = 0;
    Tick finish_time_ns = 0;
    Tick queue_delay_ns = 0;
    Tick prefill_queue_delay_ns = 0;
    Tick decode_queue_delay_ns = 0;
    Tick transfer_queue_delay_ns = 0;
    Tick prefill_duration_ns = 0;
    Tick transfer_duration_ns = 0;
    Tick decode_duration_ns = 0;
    Tick ttft_ns = 0;
    double tpot_ns = 0.0;
    Tick e2e_ns = 0;
    uint64_t kv_transfer_bytes = 0;
    uint64_t kv_resident_bytes = 0;
    ServingStageBreakdown prefill_breakdown;
    ServingStageBreakdown decode_breakdown;
    ServingStageBreakdown transfer_breakdown;
    ServingGoodputResult goodput;
};

struct ServingEventTraceRecord {
    Tick time_ns = 0;
    std::string event;
    uint64_t batch_id = 0;
    size_t worker_id = 0;
    size_t worker_group_id = 0;
    size_t replica_id = 0;
    std::string stage;
    std::string layout_name;
    std::string request_ids;
    uint64_t total_tokens = 0;
    Tick duration_ns = 0;
};

struct ServingOutputPaths {
    std::string request_configuration_path;
    std::string request_metrics_output;
    std::string request_summary_output;
    std::string request_run_metadata_output;
    std::string event_trace_output;
};

void emit_serving_request_log(const ServingRequestMetrics& metrics);
void emit_serving_summary_log(const std::string& metric_name,
                              const ServingMetricStats& stats);
void emit_serving_throughput_log(size_t total_requests,
                                 Tick makespan_ns,
                                 uint64_t total_output_tokens);
void write_serving_metrics_csv(const std::string& path,
                               const ServingConfig& config,
                               const std::vector<ServingRequestMetrics>& metrics);
void write_serving_summary_json(const std::string& path,
                                const ServingConfig& config,
                                const std::vector<ServingRequestMetrics>& metrics);
void write_serving_run_metadata_json(const std::string& path,
                                     const ServingConfig& config,
                                     const ServingOutputPaths& outputs,
                                     const std::string& binary_name);
void write_serving_event_trace_csv(
    const std::string& path,
    const ServingConfig& config,
    const std::vector<ServingEventTraceRecord>& records);

ServingMetricStats summarize_serving_metric(const std::vector<double>& values);

}  // namespace AstraSim

#endif /* __SERVING_METRICS_HH__ */
