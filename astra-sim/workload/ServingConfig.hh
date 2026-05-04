#ifndef __SERVING_CONFIG_HH__
#define __SERVING_CONFIG_HH__

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "astra-sim/system/Common.hh"

namespace AstraSim {

struct ServingCollectiveSpec {
    bool enabled;
    ComType type;
    std::string type_name;
    uint64_t size_bytes;
};

struct ServingTokenDistributionSpec {
    std::string distribution;
    uint64_t min;
    uint64_t max;
};

struct ServingTraceSpec {
    uint64_t seed;
    uint64_t num_requests;
    std::string arrival_process;
    double request_rate_per_second;
    ServingTokenDistributionSpec prompt_tokens;
    ServingTokenDistributionSpec output_tokens;
};

struct ServingRequestSpec {
    uint64_t request_id;
    Tick arrival_time_ns;
    uint64_t prompt_tokens;
    uint64_t output_tokens;
    size_t original_index;
};

struct ServingConfig {
    Tick prefill_base_latency_ns;
    Tick prefill_compute_ns_per_token;
    Tick decode_base_latency_ns;
    Tick decode_compute_ns_per_token;
    ServingCollectiveSpec prefill_collective;
    ServingCollectiveSpec decode_collective;
    std::vector<ServingRequestSpec> requests;
    std::optional<uint64_t> trace_seed;

    static ServingConfig load_from_file(const std::string& path);
    static ServingConfig load_from_json_text(const std::string& json_text,
                                            const std::string& source_name);
};

}  // namespace AstraSim

#endif /* __SERVING_CONFIG_HH__ */
