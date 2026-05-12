#ifndef ASTRASIM_COMMON_PARALLELISM_LAYOUT_HH
#define ASTRASIM_COMMON_PARALLELISM_LAYOUT_HH

#include <cstdint>
#include <optional>
#include <string>

namespace AstraSim {

enum class ServingTopologyDeployment {
    Colocated = 0,
    PrefillDecodeDisaggregated,
};

struct ParallelismLayoutSpec {
    std::string name;
    uint64_t tp_degree = 1;
    uint64_t pp_degree = 1;
    uint64_t ep_degree = 1;
    uint64_t dp_attention_degree = 1;
    uint64_t dp_replica_count = 1;
};

struct ServingTopologySpec {
    ServingTopologyDeployment deployment =
        ServingTopologyDeployment::Colocated;
    ParallelismLayoutSpec colocated_layout;
    std::optional<ParallelismLayoutSpec> prefill_layout;
    std::optional<ParallelismLayoutSpec> decode_layout;
};

inline const ParallelismLayoutSpec& effective_prefill_layout(
    const ServingTopologySpec& topology) {
    if (topology.prefill_layout.has_value()) {
        return *topology.prefill_layout;
    }
    return topology.colocated_layout;
}

inline const ParallelismLayoutSpec& effective_decode_layout(
    const ServingTopologySpec& topology) {
    if (topology.decode_layout.has_value()) {
        return *topology.decode_layout;
    }
    return topology.colocated_layout;
}

inline bool has_disaggregated_layouts(const ServingTopologySpec& topology) {
    return topology.prefill_layout.has_value() ||
           topology.decode_layout.has_value() ||
           topology.deployment ==
               ServingTopologyDeployment::PrefillDecodeDisaggregated;
}

inline std::string to_string(ServingTopologyDeployment deployment) {
    switch (deployment) {
    case ServingTopologyDeployment::Colocated:
        return "colocated";
    case ServingTopologyDeployment::PrefillDecodeDisaggregated:
        return "prefill_decode_disaggregated";
    }
    return "unknown";
}

}  // namespace AstraSim

#endif  // ASTRASIM_COMMON_PARALLELISM_LAYOUT_HH
