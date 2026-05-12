#ifndef ASTRASIM_SCENARIOS_SCENARIO_COST_MODELS_HH
#define ASTRASIM_SCENARIOS_SCENARIO_COST_MODELS_HH

#include <cstdint>
#include <vector>

#include "astra-sim/analytical/AnalyticalConfig.hh"

namespace AstraSim {

struct DenseMemoryBreakdown {
    uint64_t weight_bytes;
    uint64_t kv_cache_bytes;
    uint64_t activation_bytes;
    uint64_t workspace_bytes;
    uint64_t total_bytes;
};

const InterconnectSpec* find_interconnect(
    const std::vector<InterconnectSpec>& interconnects,
    DeviceType src_type,
    DeviceType dst_type);

double compute_time_seconds(long double flops,
                            double peak_flops,
                            double efficiency,
                            uint32_t parallel_units);
double ring_all_reduce_time_seconds(uint64_t bytes,
                                    uint32_t participants,
                                    double bandwidth_bytes_per_s,
                                    uint64_t latency_ns,
                                    double efficiency);
double ring_all_gather_time_seconds(uint64_t bytes,
                                    uint32_t participants,
                                    double bandwidth_bytes_per_s,
                                    uint64_t latency_ns,
                                    double efficiency);
double all_to_all_time_seconds(uint64_t bytes,
                               uint32_t participants,
                               double bandwidth_bytes_per_s,
                               uint64_t latency_ns,
                               double efficiency);
double point_to_point_time_seconds(uint64_t bytes,
                                   double bandwidth_bytes_per_s,
                                   uint64_t latency_ns,
                                   double efficiency);
DenseMemoryBreakdown estimate_dense_memory_per_gpu(
    const DenseModelSpec& model,
    const ClusterSpec& cluster,
    uint32_t tp_degree,
    uint32_t pp_degree,
    uint32_t batch_size,
    uint32_t sequence_length,
    uint32_t num_microbatches,
    double activation_multiplier);
double device_energy_joules(double busy_time_seconds,
                            double total_time_seconds,
                            const DeviceSpec& device);

}  // namespace AstraSim

#endif  // ASTRASIM_SCENARIOS_SCENARIO_COST_MODELS_HH
