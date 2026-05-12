#ifndef ASTRASIM_ANALYTICAL_ANALYTICAL_CONFIG_HH
#define ASTRASIM_ANALYTICAL_ANALYTICAL_CONFIG_HH

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "astra-sim/common/ParallelismLayout.hh"

namespace AstraSim {

enum class AnalyticalMode {
    tp_pp_crossover = 0,
    serving_disagg_colocated,
    attention_ffn_disaggregation,
    serving_scale,
};

enum class DeviceType {
    GPU = 0,
    LPU,
};

struct DenseModelSpec {
    std::string name;
    uint64_t parameter_count;
    uint32_t num_layers;
    uint32_t hidden_size;
    uint32_t ffn_hidden_size;
    uint32_t attention_heads;
    uint32_t kv_heads;
    uint32_t max_sequence_length;
    uint32_t bytes_per_parameter;
    uint32_t bytes_per_activation;
    uint32_t bytes_per_kv_element;

    static DenseModelSpec dense_70b_preset();
};

struct MoEModelSpec {
    std::string name;
    uint64_t total_parameter_count;
    uint64_t attention_parameter_count;
    uint64_t router_parameter_count;
    uint64_t expert_parameter_count;
    uint32_t num_layers;
    uint32_t hidden_size;
    uint32_t attention_heads;
    uint32_t kv_heads;
    uint32_t experts_per_layer;
    uint32_t active_experts_per_token;
    uint32_t expert_hidden_size;
    uint32_t max_sequence_length;
    uint32_t bytes_per_parameter;
    uint32_t bytes_per_activation;
    uint32_t bytes_per_kv_element;

    static MoEModelSpec trillion_parameter_moe_preset();
};

struct DeviceSpec {
    std::string name;
    DeviceType type;
    uint32_t count;
    uint64_t memory_capacity_bytes;
    double peak_flops;
    double memory_bandwidth_bytes_per_s;
    double active_power_w;
    double idle_power_w;
};

struct ClusterSpec {
    std::vector<DeviceSpec> devices;
    uint64_t workspace_reserve_bytes;
    std::optional<uint64_t> seed;

    const DeviceSpec* find_device(DeviceType type) const;
};

struct InterconnectSpec {
    std::string name;
    DeviceType src_type;
    DeviceType dst_type;
    double bandwidth_bytes_per_s;
    uint64_t latency_ns;
    bool full_duplex;
    double efficiency;
};

struct AnalyticalOutputPaths {
    std::string results_csv;
    std::string summary_json;
};

struct TpPpSweepConfig {
    std::vector<uint32_t> tp_degrees;
    std::vector<uint32_t> pp_degrees;
    std::vector<uint32_t> batch_sizes;
    std::vector<uint32_t> sequence_lengths;
    std::vector<uint32_t> num_microbatches;
};

struct TpPpCrossoverConfig {
    DenseModelSpec model;
    ClusterSpec cluster;
    std::vector<InterconnectSpec> interconnects;
    AnalyticalOutputPaths outputs;
    TpPpSweepConfig sweep;
    double compute_efficiency;
    double all_reduce_efficiency;
    double activation_transfer_efficiency;
    double activation_multiplier;
};

struct ServingDisaggColocatedConfig {
    std::string request_configuration;
    std::string request_metrics_output;
    std::string request_summary_output;
    std::string request_run_metadata_output;
    std::string workload_configuration;
    std::string comm_group_configuration;
    std::string system_configuration;
    std::string remote_memory_configuration;
    std::string network_configuration;
    std::string logging_configuration;
    std::string logging_folder;
    int num_queues_per_dim;
    double compute_scale;
    double comm_scale;
    double injection_scale;
    bool rendezvous_protocol;
};

struct AttentionFfnDisaggregationConfig {
    MoEModelSpec model;
    ClusterSpec cluster;
    std::vector<InterconnectSpec> interconnects;
    AnalyticalOutputPaths outputs;
    uint32_t batch_size;
    uint32_t sequence_length;
    double attention_compute_efficiency;
    double router_compute_efficiency;
    double expert_compute_efficiency;
    double transfer_efficiency;
};

struct ServingScaleConfig {
    std::optional<DenseModelSpec> dense_model;
    std::optional<MoEModelSpec> moe_model;
    ClusterSpec cluster;
    std::vector<InterconnectSpec> interconnects;
    ServingTopologySpec topology;
    std::string request_configuration;
    std::string request_metrics_output;
    std::string request_summary_output;
    std::string request_run_metadata_output;
    std::string workload_configuration;
    std::string comm_group_configuration;
    std::string system_configuration;
    std::string remote_memory_configuration;
    std::string network_configuration;
    std::string logging_configuration;
    std::string logging_folder;
    int num_queues_per_dim = 1;
    double compute_scale = 1.0;
    double comm_scale = 1.0;
    double injection_scale = 1.0;
    bool rendezvous_protocol = false;
};

struct AnalyticalConfig {
    AnalyticalMode mode;
    std::string source_path;
    std::optional<TpPpCrossoverConfig> tp_pp_crossover;
    std::optional<ServingDisaggColocatedConfig> serving_disagg_colocated;
    std::optional<AttentionFfnDisaggregationConfig> attention_ffn_disaggregation;
    std::optional<ServingScaleConfig> serving_scale;

    static AnalyticalConfig load_from_file(const std::string& path);
    static AnalyticalConfig load_from_yaml_text(const std::string& yaml_text,
                                                const std::string& source_name);
};

std::string to_string(AnalyticalMode mode);
std::string to_string(DeviceType type);

}  // namespace AstraSim

#endif  // ASTRASIM_ANALYTICAL_ANALYTICAL_CONFIG_HH
