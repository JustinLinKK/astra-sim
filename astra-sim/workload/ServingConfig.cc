#include "astra-sim/workload/ServingConfig.hh"

#include <cmath>
#include <fstream>
#include <json/json.hpp>
#include <random>
#include <sstream>
#include <stdexcept>

#include "astra-sim/common/Logging.hh"

using json = nlohmann::json;

namespace AstraSim {

namespace {

[[noreturn]] void serving_config_error(const std::string& message) {
    LoggerFactory::get_logger("serving")->critical(message);
    throw std::runtime_error(message);
}

void require_object(const json& parent,
                    const std::string& field_name,
                    const std::string& source_name) {
    if (!parent.contains(field_name) || !parent[field_name].is_object()) {
        serving_config_error("Serving config field '" + field_name +
                             "' must be an object in " + source_name);
    }
}

void require_unsigned_integer(const json& parent,
                              const std::string& field_name,
                              const std::string& source_name) {
    if (!parent.contains(field_name) ||
        !parent[field_name].is_number_unsigned()) {
        serving_config_error("Serving config field '" + field_name +
                             "' must be an unsigned integer in " +
                             source_name);
    }
}

void require_number(const json& parent,
                    const std::string& field_name,
                    const std::string& source_name) {
    if (!parent.contains(field_name) || !parent[field_name].is_number()) {
        serving_config_error("Serving config field '" + field_name +
                             "' must be a number in " + source_name);
    }
}

void require_string(const json& parent,
                    const std::string& field_name,
                    const std::string& source_name) {
    if (!parent.contains(field_name) || !parent[field_name].is_string()) {
        serving_config_error("Serving config field '" + field_name +
                             "' must be a string in " + source_name);
    }
}

ComType parse_collective_type(const json& collective_json,
                              const std::string& field_name,
                              const std::string& source_name) {
    require_string(collective_json, "type", source_name + " (" + field_name + ")");

    const auto type_name = collective_json["type"].get<std::string>();
    if (type_name == "all-reduce") {
        return ComType::All_Reduce;
    }
    if (type_name == "all-gather") {
        return ComType::All_Gather;
    }
    if (type_name == "reduce-scatter") {
        return ComType::Reduce_Scatter;
    }
    if (type_name == "all-to-all") {
        return ComType::All_to_All;
    }

    serving_config_error("Unsupported serving collective type '" + type_name +
                         "' in field '" + field_name + ".type' from " +
                         source_name);
}

ServingCollectiveSpec parse_collective(const json& parent,
                                       const std::string& field_name,
                                       const std::string& source_name) {
    require_object(parent, field_name, source_name);
    const auto& collective_json = parent[field_name];

    if (!collective_json.contains("enabled") ||
        !collective_json["enabled"].is_boolean()) {
        serving_config_error("Serving config field '" + field_name +
                             ".enabled' must be a boolean in " + source_name);
    }
    require_unsigned_integer(collective_json, "size_bytes", source_name);

    ServingCollectiveSpec collective;
    collective.enabled = collective_json["enabled"].get<bool>();
    collective.type =
        parse_collective_type(collective_json, field_name, source_name);
    collective.type_name = collective_json["type"].get<std::string>();
    collective.size_bytes = collective_json["size_bytes"].get<uint64_t>();
    return collective;
}

ServingTokenDistributionSpec parse_distribution(const json& parent,
                                                const std::string& field_name,
                                                const std::string& source_name) {
    require_object(parent, field_name, source_name);
    const auto& distribution_json = parent[field_name];
    require_string(distribution_json, "distribution", source_name);
    require_unsigned_integer(distribution_json, "min", source_name);
    require_unsigned_integer(distribution_json, "max", source_name);

    ServingTokenDistributionSpec distribution;
    distribution.distribution =
        distribution_json["distribution"].get<std::string>();
    distribution.min = distribution_json["min"].get<uint64_t>();
    distribution.max = distribution_json["max"].get<uint64_t>();

    if (distribution.distribution != "uniform") {
        serving_config_error("Unsupported serving token distribution '" +
                             distribution.distribution + "' in field '" +
                             field_name + "' from " + source_name);
    }
    if (distribution.min < 1 || distribution.max < distribution.min) {
        serving_config_error("Serving distribution field '" + field_name +
                             "' must satisfy 1 <= min <= max in " +
                             source_name);
    }

    return distribution;
}

std::vector<ServingRequestSpec> parse_explicit_requests(
    const json& requests_json,
    const std::string& source_name) {
    std::vector<ServingRequestSpec> requests;
    size_t original_index = 0;
    for (const auto& request_json : requests_json) {
        if (!request_json.is_object()) {
            serving_config_error("Each serving request must be an object in " +
                                 source_name);
        }
        require_unsigned_integer(request_json, "request_id", source_name);
        require_unsigned_integer(request_json, "arrival_time_ns", source_name);
        require_unsigned_integer(request_json, "prompt_tokens", source_name);
        require_unsigned_integer(request_json, "output_tokens", source_name);

        const auto prompt_tokens = request_json["prompt_tokens"].get<uint64_t>();
        const auto output_tokens = request_json["output_tokens"].get<uint64_t>();
        if (prompt_tokens == 0) {
            serving_config_error(
                "Serving request field 'prompt_tokens' must be greater than zero in " +
                source_name);
        }
        if (output_tokens == 0) {
            serving_config_error(
                "Serving request field 'output_tokens' must be greater than zero in " +
                source_name);
        }

        ServingRequestSpec request;
        request.request_id = request_json["request_id"].get<uint64_t>();
        request.arrival_time_ns =
            request_json["arrival_time_ns"].get<uint64_t>();
        request.prompt_tokens = prompt_tokens;
        request.output_tokens = output_tokens;
        request.original_index = original_index++;
        requests.push_back(request);
    }
    return requests;
}

ServingTraceSpec parse_trace(const json& root, const std::string& source_name) {
    require_object(root, "trace", source_name);
    const auto& trace_json = root["trace"];

    require_unsigned_integer(trace_json, "seed", source_name);
    require_unsigned_integer(trace_json, "num_requests", source_name);
    require_string(trace_json, "arrival_process", source_name);
    require_number(trace_json, "request_rate_per_second", source_name);

    ServingTraceSpec trace;
    trace.seed = trace_json["seed"].get<uint64_t>();
    trace.num_requests = trace_json["num_requests"].get<uint64_t>();
    trace.arrival_process = trace_json["arrival_process"].get<std::string>();
    trace.request_rate_per_second =
        trace_json["request_rate_per_second"].get<double>();
    trace.prompt_tokens = parse_distribution(trace_json, "prompt_tokens",
                                             source_name);
    trace.output_tokens = parse_distribution(trace_json, "output_tokens",
                                             source_name);

    if (trace.arrival_process != "poisson") {
        serving_config_error("Unsupported serving arrival process '" +
                             trace.arrival_process + "' in " + source_name);
    }
    if (trace.num_requests > 0 && trace.request_rate_per_second <= 0.0) {
        serving_config_error(
            "Serving trace field 'request_rate_per_second' must be positive in " +
            source_name);
    }
    return trace;
}

std::vector<ServingRequestSpec> generate_trace_requests(
    const ServingTraceSpec& trace) {
    std::vector<ServingRequestSpec> requests;
    requests.reserve(trace.num_requests);

    std::mt19937_64 rng(trace.seed);
    std::exponential_distribution<double> interarrival_distribution(
        trace.request_rate_per_second);
    std::uniform_int_distribution<uint64_t> prompt_distribution(
        trace.prompt_tokens.min, trace.prompt_tokens.max);
    std::uniform_int_distribution<uint64_t> output_distribution(
        trace.output_tokens.min, trace.output_tokens.max);

    long double arrival_time_seconds = 0.0L;
    for (uint64_t request_id = 0; request_id < trace.num_requests; ++request_id) {
        if (request_id > 0) {
            arrival_time_seconds += static_cast<long double>(
                interarrival_distribution(rng));
        }

        ServingRequestSpec request;
        request.request_id = request_id;
        request.arrival_time_ns = static_cast<Tick>(
            std::llround(arrival_time_seconds * 1000000000.0L));
        request.prompt_tokens = prompt_distribution(rng);
        request.output_tokens = output_distribution(rng);
        request.original_index = static_cast<size_t>(request_id);
        requests.push_back(request);
    }

    return requests;
}

ServingConfig parse_serving_config(const json& root,
                                   const std::string& source_name) {
    if (!root.contains("baseline") || !root["baseline"].is_object()) {
        serving_config_error("Serving config field 'baseline' must be an object in " +
                             source_name);
    }

    const bool has_requests = root.contains("requests");
    const bool has_trace = root.contains("trace");
    if (has_requests == has_trace) {
        serving_config_error(
            "Serving config must contain exactly one of 'requests' or 'trace' in " +
            source_name);
    }

    const auto& baseline = root["baseline"];
    require_unsigned_integer(baseline, "prefill_base_latency_ns", source_name);
    require_unsigned_integer(baseline, "prefill_compute_ns_per_token",
                             source_name);
    require_unsigned_integer(baseline, "decode_base_latency_ns", source_name);
    require_unsigned_integer(baseline, "decode_compute_ns_per_token",
                             source_name);

    ServingConfig config;
    config.prefill_base_latency_ns =
        baseline["prefill_base_latency_ns"].get<uint64_t>();
    config.prefill_compute_ns_per_token =
        baseline["prefill_compute_ns_per_token"].get<uint64_t>();
    config.decode_base_latency_ns =
        baseline["decode_base_latency_ns"].get<uint64_t>();
    config.decode_compute_ns_per_token =
        baseline["decode_compute_ns_per_token"].get<uint64_t>();
    config.prefill_collective = parse_collective(baseline, "prefill_collective",
                                                 source_name);
    config.decode_collective = parse_collective(baseline, "decode_collective",
                                                source_name);

    if (has_requests) {
        if (!root["requests"].is_array()) {
            serving_config_error("Serving config field 'requests' must be an array in " +
                                 source_name);
        }
        config.requests = parse_explicit_requests(root["requests"], source_name);
        config.trace_seed = std::nullopt;
    } else {
        const auto trace = parse_trace(root, source_name);
        config.requests = generate_trace_requests(trace);
        config.trace_seed = trace.seed;
    }

    return config;
}

}  // namespace

ServingConfig ServingConfig::load_from_file(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        serving_config_error("Unable to open serving request configuration: " +
                             path);
    }

    std::stringstream buffer;
    buffer << input.rdbuf();
    return load_from_json_text(buffer.str(), path);
}

ServingConfig ServingConfig::load_from_json_text(const std::string& json_text,
                                                 const std::string& source_name) {
    json root;
    try {
        root = json::parse(json_text);
    } catch (const std::exception& e) {
        serving_config_error("Failed to parse serving request configuration '" +
                             source_name + "': " + e.what());
    }

    return parse_serving_config(root, source_name);
}

}  // namespace AstraSim
