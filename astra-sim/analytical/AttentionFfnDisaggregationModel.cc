#include "astra-sim/analytical/AttentionFfnDisaggregationModel.hh"

#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <json/json.hpp>

#include "astra-sim/analytical/AnalyticalCostModels.hh"
#include "astra-sim/analytical/AnalyticalResult.hh"

using json = nlohmann::json;

namespace AstraSim {

namespace {

struct PlacementResult {
    std::string placement;
    double tokens_per_sec;
    double joules_per_token;
    double tokens_per_joule;
    double tokens_per_sec_per_w;
    double gpu_utilization;
    double lpu_utilization;
    uint64_t transfer_overhead_ns;
};

uint64_t to_ns(double seconds) {
    return static_cast<uint64_t>(std::llround(seconds * 1.0e9));
}

std::string format_double(double value, int precision = 6) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

long double attention_flops_per_layer(uint32_t batch_size,
                                      uint32_t sequence_length,
                                      const MoEModelSpec& model) {
    const auto batch = static_cast<long double>(batch_size);
    const auto seq = static_cast<long double>(sequence_length);
    const auto hidden = static_cast<long double>(model.hidden_size);
    return 8.0L * batch * hidden * hidden + 4.0L * batch * seq * hidden;
}

long double router_flops_per_layer(uint32_t batch_size,
                                   const MoEModelSpec& model) {
    return 2.0L * static_cast<long double>(batch_size) *
           static_cast<long double>(model.hidden_size) *
           static_cast<long double>(model.experts_per_layer);
}

long double expert_flops_per_layer(uint32_t batch_size,
                                   const MoEModelSpec& model) {
    return 6.0L * static_cast<long double>(batch_size) *
           static_cast<long double>(model.active_experts_per_token) *
           static_cast<long double>(model.hidden_size) *
           static_cast<long double>(model.expert_hidden_size);
}

uint64_t transfer_bytes_per_layer(uint32_t batch_size,
                                  const MoEModelSpec& model) {
    return static_cast<uint64_t>(
        static_cast<long double>(batch_size) *
        static_cast<long double>(model.hidden_size) *
        static_cast<long double>(model.bytes_per_activation));
}

PlacementResult evaluate_baseline(
    const AttentionFfnDisaggregationConfig& config,
    const DeviceSpec& gpu) {
    const auto attention_seconds = compute_time_seconds(
        attention_flops_per_layer(config.batch_size, config.sequence_length,
                                  config.model) *
            config.model.num_layers,
        gpu.peak_flops, config.attention_compute_efficiency, gpu.count);
    const auto router_seconds = compute_time_seconds(
        router_flops_per_layer(config.batch_size, config.model) *
            config.model.num_layers,
        gpu.peak_flops, config.router_compute_efficiency, gpu.count);
    const auto expert_seconds = compute_time_seconds(
        expert_flops_per_layer(config.batch_size, config.model) *
            config.model.num_layers,
        gpu.peak_flops, config.expert_compute_efficiency, gpu.count);

    const auto total_seconds =
        attention_seconds + router_seconds + expert_seconds;
    const auto gpu_busy_seconds = total_seconds;
    const auto gpu_energy =
        device_energy_joules(gpu_busy_seconds, total_seconds, gpu) * gpu.count;
    const auto tokens_per_sec = total_seconds > 0.0
                                    ? static_cast<double>(config.batch_size) /
                                          total_seconds
                                    : 0.0;
    const auto average_power_w =
        total_seconds > 0.0 ? gpu_energy / total_seconds : 0.0;

    return PlacementResult{
        "homogeneous_gpu",
        tokens_per_sec,
        config.batch_size > 0 ? gpu_energy / config.batch_size : 0.0,
        gpu_energy > 0.0 ? config.batch_size / gpu_energy : 0.0,
        average_power_w > 0.0 ? tokens_per_sec / average_power_w : 0.0,
        total_seconds > 0.0 ? gpu_busy_seconds / total_seconds : 0.0,
        0.0,
        0,
    };
}

PlacementResult evaluate_heterogeneous(
    const AttentionFfnDisaggregationConfig& config,
    const DeviceSpec& gpu,
    const DeviceSpec& lpu,
    const InterconnectSpec& gpu_to_lpu) {
    const auto attention_seconds = compute_time_seconds(
        attention_flops_per_layer(config.batch_size, config.sequence_length,
                                  config.model) *
            config.model.num_layers,
        gpu.peak_flops, config.attention_compute_efficiency, gpu.count);
    const auto router_seconds = compute_time_seconds(
        router_flops_per_layer(config.batch_size, config.model) *
            config.model.num_layers,
        gpu.peak_flops, config.router_compute_efficiency, gpu.count);
    const auto expert_seconds = compute_time_seconds(
        expert_flops_per_layer(config.batch_size, config.model) *
            config.model.num_layers,
        lpu.peak_flops, config.expert_compute_efficiency, lpu.count);
    const auto transfer_seconds = 2.0 * config.model.num_layers *
                                  point_to_point_time_seconds(
                                      transfer_bytes_per_layer(config.batch_size,
                                                               config.model),
                                      gpu_to_lpu.bandwidth_bytes_per_s,
                                      gpu_to_lpu.latency_ns,
                                      config.transfer_efficiency);
    const auto total_seconds =
        attention_seconds + router_seconds + expert_seconds + transfer_seconds;
    const auto gpu_busy_seconds = attention_seconds + router_seconds;
    const auto lpu_busy_seconds = expert_seconds;
    const auto gpu_energy =
        device_energy_joules(gpu_busy_seconds, total_seconds, gpu) * gpu.count;
    const auto lpu_energy =
        device_energy_joules(lpu_busy_seconds, total_seconds, lpu) * lpu.count;
    const auto total_energy = gpu_energy + lpu_energy;
    const auto tokens_per_sec = total_seconds > 0.0
                                    ? static_cast<double>(config.batch_size) /
                                          total_seconds
                                    : 0.0;
    const auto average_power_w =
        total_seconds > 0.0 ? total_energy / total_seconds : 0.0;

    return PlacementResult{
        "heterogeneous_gpu_lpu",
        tokens_per_sec,
        config.batch_size > 0 ? total_energy / config.batch_size : 0.0,
        total_energy > 0.0 ? config.batch_size / total_energy : 0.0,
        average_power_w > 0.0 ? tokens_per_sec / average_power_w : 0.0,
        total_seconds > 0.0 ? gpu_busy_seconds / total_seconds : 0.0,
        total_seconds > 0.0 ? lpu_busy_seconds / total_seconds : 0.0,
        to_ns(transfer_seconds),
    };
}

}  // namespace

AttentionFfnDisaggregationModel::AttentionFfnDisaggregationModel(
    AttentionFfnDisaggregationConfig config)
    : config(std::move(config)) {}

void AttentionFfnDisaggregationModel::run() {
    const auto* gpu = config.cluster.find_device(DeviceType::GPU);
    const auto* lpu = config.cluster.find_device(DeviceType::LPU);
    if (gpu == nullptr || lpu == nullptr) {
        throw std::runtime_error(
            "Attention/FFN disaggregation mode requires both GPU and LPU device specs");
    }
    const auto* gpu_to_lpu =
        find_interconnect(config.interconnects, DeviceType::GPU, DeviceType::LPU);
    if (gpu_to_lpu == nullptr) {
        throw std::runtime_error(
            "Attention/FFN disaggregation mode requires a GPU-to-LPU interconnect spec");
    }

    const auto baseline = evaluate_baseline(config, *gpu);
    const auto heterogeneous =
        evaluate_heterogeneous(config, *gpu, *lpu, *gpu_to_lpu);

    AnalyticalTable table{
        {"placement",
         "tokens_per_sec",
         "joules_per_token",
         "tokens_per_joule",
         "tokens_per_sec_per_w",
         "gpu_utilization",
         "lpu_utilization",
         "transfer_overhead_ns"},
        {},
    };
    for (const auto& row : {baseline, heterogeneous}) {
        table.add_row({row.placement,
                       format_double(row.tokens_per_sec),
                       format_double(row.joules_per_token),
                       format_double(row.tokens_per_joule),
                       format_double(row.tokens_per_sec_per_w),
                       format_double(row.gpu_utilization),
                       format_double(row.lpu_utilization),
                       std::to_string(row.transfer_overhead_ns)});
    }

    const json summary{
        {"mode", to_string(AnalyticalMode::attention_ffn_disaggregation)},
        {"model_name", config.model.name},
        {"batch_size", config.batch_size},
        {"sequence_length", config.sequence_length},
        {"placements",
         {
             {"homogeneous_gpu",
              {{"tokens_per_sec", baseline.tokens_per_sec},
               {"joules_per_token", baseline.joules_per_token},
               {"tokens_per_joule", baseline.tokens_per_joule},
               {"tokens_per_sec_per_w", baseline.tokens_per_sec_per_w},
               {"gpu_utilization", baseline.gpu_utilization},
               {"lpu_utilization", baseline.lpu_utilization},
               {"transfer_overhead_ns", baseline.transfer_overhead_ns}}},
             {"heterogeneous_gpu_lpu",
              {{"tokens_per_sec", heterogeneous.tokens_per_sec},
               {"joules_per_token", heterogeneous.joules_per_token},
               {"tokens_per_joule", heterogeneous.tokens_per_joule},
               {"tokens_per_sec_per_w", heterogeneous.tokens_per_sec_per_w},
               {"gpu_utilization", heterogeneous.gpu_utilization},
               {"lpu_utilization", heterogeneous.lpu_utilization},
               {"transfer_overhead_ns", heterogeneous.transfer_overhead_ns}}},
         }},
        {"seed", config.cluster.seed ? json(*config.cluster.seed) : json(nullptr)},
    };

    AnalyticalResultWriter::write_csv(table, config.outputs.results_csv);
    AnalyticalResultWriter::write_json(summary, config.outputs.summary_json);
}

}  // namespace AstraSim
