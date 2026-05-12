#ifndef __SERVING_CONFIG_HH__
#define __SERVING_CONFIG_HH__

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "astra-sim/analytical/AnalyticalConfig.hh"
#include "astra-sim/common/ParallelismLayout.hh"
#include "astra-sim/system/Common.hh"
#include "astra-sim/workload/ServingTypes.hh"

namespace AstraSim {

struct ServingCollectiveSpec {
    bool enabled = false;
    ComType type = ComType::None;
    std::string type_name;
    uint64_t size_bytes = 0;
};

struct ServingTokenDistributionSpec {
    std::string distribution;
    uint64_t min = 0;
    uint64_t max = 0;
};

struct ServingTraceSpec {
    uint64_t seed = 0;
    uint64_t num_requests = 0;
    std::string arrival_process;
    double request_rate_per_second = 0.0;
    ServingTokenDistributionSpec prompt_tokens;
    ServingTokenDistributionSpec output_tokens;
};

struct ServingRuntimeConfig {
    ServingArchitecture architecture = ServingArchitecture::SerialBaseline;
    std::optional<uint64_t> seed;
};

struct ServingSchedulerConfig {
    uint64_t max_running_requests = 1;
    ServingSchedulerPolicy scheduler_policy =
        ServingSchedulerPolicy::Serial;
    uint64_t max_prefill_batch_tokens = 0;
    uint64_t max_decode_batch_requests = 1;
    uint64_t chunked_prefill_size = 0;
    uint64_t prefill_max_requests = 1;
    bool enable_mixed_chunk = false;
};

struct ServingSloConfig {
    bool enabled = false;
    Tick ttft_ns = 0;
    double tpot_ns = 0.0;
    std::optional<Tick> e2e_ns;
};

struct ServingModelConfig {
    std::string name;
    std::string kind = "dense";
    uint64_t num_layers = 0;
    uint64_t hidden_size = 0;
    uint64_t ffn_hidden_size = 0;
    uint64_t attention_heads = 0;
    uint64_t kv_heads = 0;
    uint64_t head_dim = 0;
    uint64_t experts_per_layer = 0;
    uint64_t active_experts_per_token = 0;
    uint64_t expert_hidden_size = 0;
    uint64_t bytes_per_parameter = 0;
    uint64_t bytes_per_activation = 0;
    uint64_t bytes_per_kv_element = 0;
};

struct ServingTransferConfig {
    bool enabled = false;
    Tick latency_ns = 0;
    double bandwidth_bytes_per_s = 0.0;
    double efficiency = 1.0;
    bool overlap_enabled = false;
    uint64_t bytes_per_prompt_token = 0;
    bool override_bytes_per_prompt_token = false;
};

struct ServingPdConfig {
    uint64_t prefill_workers = 0;
    uint64_t decode_workers = 0;
    uint64_t prefill_max_batch_tokens = 0;
    uint64_t prefill_max_requests = 1;
    uint64_t decode_max_batch_requests = 1;
    uint64_t prefill_tp_degree = 1;
    uint64_t decode_tp_degree = 1;
    ServingTransferConfig transfer;
};

struct ServingOutputsConfig {
    std::string event_trace_output;
};

struct ServingCostModelConfig {
    struct StageComponentConfig {
        Tick base_latency_ns = 0;
        Tick attention_compute_ns_per_token = 0;
        Tick ffn_or_expert_compute_ns_per_token = 0;
        Tick tp_collective_ns_per_token = 0;
        Tick pp_activation_ns_per_token = 0;
        Tick ep_dispatch_ns_per_token = 0;
        Tick dp_attention_sync_ns_per_token = 0;
        double batch_efficiency = 1.0;
        double interference_factor = 1.0;
    };

    Tick prefill_base_latency_ns = 0;
    Tick prefill_compute_ns_per_token = 0;
    Tick decode_base_latency_ns = 0;
    Tick decode_compute_ns_per_token = 0;
    double prefill_batch_efficiency = 1.0;
    double decode_batch_efficiency = 1.0;
    double decode_interference_factor = 1.0;
    double mixed_prefill_weight = 1.0;
    bool topology_aware_enabled = false;
    StageComponentConfig prefill;
    StageComponentConfig decode;
};

struct ServingRequestSpec {
    uint64_t request_id = 0;
    Tick arrival_time_ns = 0;
    uint64_t prompt_tokens = 0;
    uint64_t output_tokens = 0;
    size_t original_index = 0;
};

struct ServingConfig {
    ServingRuntimeConfig runtime;
    ServingSchedulerConfig scheduler;
    ServingSloConfig slo;
    std::optional<ServingModelConfig> model;
    std::optional<ServingPdConfig> pd;
    ServingOutputsConfig outputs;
    ServingCostModelConfig cost_model;
    ServingCollectiveSpec prefill_collective;
    ServingCollectiveSpec decode_collective;
    std::optional<ServingTopologySpec> topology;
    std::optional<ClusterSpec> cluster;
    std::vector<InterconnectSpec> interconnects;
    std::vector<ServingRequestSpec> requests;
    std::optional<uint64_t> trace_seed;

    static ServingConfig load_from_file(const std::string& path);
    static ServingConfig load_from_json_text(const std::string& json_text,
                                            const std::string& source_name);
};

}  // namespace AstraSim

#endif /* __SERVING_CONFIG_HH__ */
