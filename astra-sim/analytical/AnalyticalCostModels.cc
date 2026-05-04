#include "astra-sim/analytical/AnalyticalCostModels.hh"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace AstraSim {

const InterconnectSpec* find_interconnect(
    const std::vector<InterconnectSpec>& interconnects,
    DeviceType src_type,
    DeviceType dst_type) {
    for (const auto& interconnect : interconnects) {
        if (interconnect.src_type == src_type &&
            interconnect.dst_type == dst_type) {
            return &interconnect;
        }
    }
    return nullptr;
}

double compute_time_seconds(long double flops,
                            double peak_flops,
                            double efficiency,
                            uint32_t parallel_units) {
    if (peak_flops <= 0.0 || efficiency <= 0.0 || parallel_units == 0) {
        return 0.0;
    }
    return static_cast<double>(
        flops /
        (static_cast<long double>(peak_flops) * efficiency *
         static_cast<long double>(parallel_units)));
}

double ring_all_reduce_time_seconds(uint64_t bytes,
                                    uint32_t participants,
                                    double bandwidth_bytes_per_s,
                                    uint64_t latency_ns,
                                    double efficiency) {
    if (bytes == 0 || participants <= 1 || bandwidth_bytes_per_s <= 0.0 ||
        efficiency <= 0.0) {
        return 0.0;
    }

    const auto n = static_cast<long double>(participants);
    const auto alpha = static_cast<long double>(latency_ns) / 1.0e9L;
    const auto beta = 1.0L /
                      (static_cast<long double>(bandwidth_bytes_per_s) *
                       static_cast<long double>(efficiency));
    return static_cast<double>(2.0L * (n - 1.0L) * alpha +
                               2.0L * (n - 1.0L) / n *
                                   static_cast<long double>(bytes) * beta);
}

double point_to_point_time_seconds(uint64_t bytes,
                                   double bandwidth_bytes_per_s,
                                   uint64_t latency_ns,
                                   double efficiency) {
    if (bytes == 0 || bandwidth_bytes_per_s <= 0.0 || efficiency <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(static_cast<long double>(latency_ns) / 1.0e9L +
                               static_cast<long double>(bytes) /
                                   (static_cast<long double>(
                                        bandwidth_bytes_per_s) *
                                    static_cast<long double>(efficiency)));
}

DenseMemoryBreakdown estimate_dense_memory_per_gpu(
    const DenseModelSpec& model,
    const ClusterSpec& cluster,
    uint32_t tp_degree,
    uint32_t pp_degree,
    uint32_t batch_size,
    uint32_t sequence_length,
    uint32_t num_microbatches,
    double activation_multiplier) {
    if (tp_degree == 0 || pp_degree == 0 || num_microbatches == 0) {
        throw std::runtime_error("Parallel degrees and microbatches must be positive");
    }

    const auto layers_per_stage =
        static_cast<long double>(model.num_layers) /
        static_cast<long double>(pp_degree);
    const auto microbatch_batch =
        static_cast<uint32_t>(std::ceil(static_cast<long double>(batch_size) /
                                        static_cast<long double>(num_microbatches)));
    const auto total_weight_bytes = static_cast<long double>(model.parameter_count) *
                                    static_cast<long double>(
                                        model.bytes_per_parameter);
    const auto weight_bytes =
        static_cast<uint64_t>(std::llround(total_weight_bytes /
                                           static_cast<long double>(tp_degree) /
                                           static_cast<long double>(pp_degree)));

    const auto kv_elements = 2.0L * static_cast<long double>(microbatch_batch) *
                             static_cast<long double>(sequence_length) *
                             layers_per_stage *
                             static_cast<long double>(model.hidden_size) /
                             static_cast<long double>(tp_degree);
    const auto kv_cache_bytes = static_cast<uint64_t>(std::llround(
        kv_elements * static_cast<long double>(model.bytes_per_kv_element)));

    const auto activation_elements =
        static_cast<long double>(microbatch_batch) *
        static_cast<long double>(sequence_length) *
        static_cast<long double>(model.hidden_size) * activation_multiplier;
    const auto activation_bytes = static_cast<uint64_t>(std::llround(
        activation_elements *
        static_cast<long double>(model.bytes_per_activation)));

    const auto total_bytes = weight_bytes + kv_cache_bytes + activation_bytes +
                             cluster.workspace_reserve_bytes;

    return DenseMemoryBreakdown{
        weight_bytes,
        kv_cache_bytes,
        activation_bytes,
        cluster.workspace_reserve_bytes,
        total_bytes,
    };
}

double device_energy_joules(double busy_time_seconds,
                            double total_time_seconds,
                            const DeviceSpec& device) {
    const auto clamped_total = std::max(total_time_seconds, 0.0);
    const auto clamped_busy = std::clamp(busy_time_seconds, 0.0, clamped_total);
    const auto idle_time = clamped_total - clamped_busy;
    return clamped_busy * device.active_power_w +
           idle_time * device.idle_power_w;
}

}  // namespace AstraSim
