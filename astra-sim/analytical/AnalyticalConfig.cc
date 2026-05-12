#include "astra-sim/analytical/AnalyticalConfig.hh"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

namespace AstraSim {

namespace {

[[noreturn]] void analytical_config_error(const std::string& message) {
    throw std::runtime_error(message);
}

std::filesystem::path base_directory_for_source(const std::string& source_name) {
    if (source_name.empty() || source_name.front() == '<') {
        return std::filesystem::current_path();
    }
    return std::filesystem::absolute(std::filesystem::path(source_name))
        .parent_path();
}

std::string resolve_path(const std::string& path_value,
                         const std::filesystem::path& base_directory) {
    if (path_value.empty() || path_value == "empty") {
        return path_value;
    }
    const auto path = std::filesystem::path(path_value);
    if (path.is_absolute()) {
        return path.lexically_normal().string();
    }
    return std::filesystem::absolute(base_directory / path)
        .lexically_normal()
        .string();
}

YAML::Node require_node(const YAML::Node& parent,
                        const std::string& field_name,
                        const std::string& source_name) {
    const auto child = parent[field_name];
    if (!child) {
        analytical_config_error("Missing required analytical config field '" +
                                field_name + "' in " + source_name);
    }
    return child;
}

template <typename T>
T parse_scalar(const YAML::Node& parent,
               const std::string& field_name,
               const std::string& source_name) {
    try {
        return require_node(parent, field_name, source_name).as<T>();
    } catch (const YAML::Exception& error) {
        analytical_config_error("Invalid value for field '" + field_name +
                                "' in " + source_name + ": " + error.what());
    }
}

template <typename T>
std::vector<T> parse_sorted_list(const YAML::Node& parent,
                                 const std::string& field_name,
                                 const std::string& source_name) {
    const auto node = require_node(parent, field_name, source_name);
    if (!node.IsSequence() || node.size() == 0) {
        analytical_config_error("Analytical config field '" + field_name +
                                "' must be a non-empty sequence in " +
                                source_name);
    }

    std::vector<T> values;
    values.reserve(node.size());
    try {
        for (const auto& child : node) {
            values.push_back(child.as<T>());
        }
    } catch (const YAML::Exception& error) {
        analytical_config_error("Invalid sequence value for field '" +
                                field_name + "' in " + source_name + ": " +
                                error.what());
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

AnalyticalMode parse_analytical_mode(const std::string& name,
                                     const std::string& source_name) {
    if (name == "tp_pp_crossover") {
        return AnalyticalMode::tp_pp_crossover;
    }
    if (name == "serving_disagg_colocated") {
        return AnalyticalMode::serving_disagg_colocated;
    }
    if (name == "attention_ffn_disaggregation") {
        return AnalyticalMode::attention_ffn_disaggregation;
    }
    if (name == "serving_scale") {
        return AnalyticalMode::serving_scale;
    }

    analytical_config_error("Unsupported analytical mode '" + name + "' in " +
                            source_name);
}

DeviceType parse_device_type(const std::string& name,
                             const std::string& source_name) {
    if (name == "GPU") {
        return DeviceType::GPU;
    }
    if (name == "LPU") {
        return DeviceType::LPU;
    }
    analytical_config_error("Unsupported device type '" + name + "' in " +
                            source_name);
}

ServingTopologyDeployment parse_topology_deployment(
    const std::string& name,
    const std::string& source_name) {
    if (name == "colocated") {
        return ServingTopologyDeployment::Colocated;
    }
    if (name == "prefill_decode_disaggregated" || name == "pd_disaggregated") {
        return ServingTopologyDeployment::PrefillDecodeDisaggregated;
    }
    analytical_config_error("Unsupported serving topology deployment '" + name +
                            "' in " + source_name);
}

ParallelismLayoutSpec parse_parallelism_layout(const YAML::Node& node,
                                               const std::string& source_name,
                                               const std::string& default_name) {
    ParallelismLayoutSpec layout{};
    layout.name = default_name;
    if (node["name"]) {
        layout.name = node["name"].as<std::string>();
    }
    if (node["tp_degree"]) {
        layout.tp_degree = node["tp_degree"].as<uint64_t>();
    }
    if (node["pp_degree"]) {
        layout.pp_degree = node["pp_degree"].as<uint64_t>();
    }
    if (node["ep_degree"]) {
        layout.ep_degree = node["ep_degree"].as<uint64_t>();
    }
    if (node["dp_attention_degree"]) {
        layout.dp_attention_degree = node["dp_attention_degree"].as<uint64_t>();
    }
    if (node["dp_replica_count"]) {
        layout.dp_replica_count = node["dp_replica_count"].as<uint64_t>();
    }
    if (layout.tp_degree == 0 || layout.pp_degree == 0 || layout.ep_degree == 0 ||
        layout.dp_attention_degree == 0 || layout.dp_replica_count == 0) {
        analytical_config_error("Parallelism layout '" + layout.name +
                                "' must use positive degrees in " + source_name);
    }
    return layout;
}

ServingTopologySpec parse_serving_topology(const YAML::Node& node,
                                           const std::string& source_name) {
    ServingTopologySpec topology{};
    if (node["deployment"]) {
        topology.deployment = parse_topology_deployment(
            node["deployment"].as<std::string>(), source_name);
    }
    if (node["colocated_layout"]) {
        topology.colocated_layout = parse_parallelism_layout(
            node["colocated_layout"], source_name, "colocated");
    } else {
        topology.colocated_layout.name = "colocated";
    }
    if (node["prefill_layout"]) {
        topology.prefill_layout = parse_parallelism_layout(
            node["prefill_layout"], source_name, "prefill");
    }
    if (node["decode_layout"]) {
        topology.decode_layout = parse_parallelism_layout(
            node["decode_layout"], source_name, "decode");
    }
    return topology;
}

void apply_dense_model_overrides(DenseModelSpec& spec,
                                 const YAML::Node& node,
                                 const std::string& source_name) {
    if (node["name"]) {
        spec.name = node["name"].as<std::string>();
    }
    if (node["parameter_count"]) {
        spec.parameter_count = node["parameter_count"].as<uint64_t>();
    }
    if (node["num_layers"]) {
        spec.num_layers = node["num_layers"].as<uint32_t>();
    }
    if (node["hidden_size"]) {
        spec.hidden_size = node["hidden_size"].as<uint32_t>();
    }
    if (node["ffn_hidden_size"]) {
        spec.ffn_hidden_size = node["ffn_hidden_size"].as<uint32_t>();
    }
    if (node["attention_heads"]) {
        spec.attention_heads = node["attention_heads"].as<uint32_t>();
    }
    if (node["kv_heads"]) {
        spec.kv_heads = node["kv_heads"].as<uint32_t>();
    }
    if (node["max_sequence_length"]) {
        spec.max_sequence_length = node["max_sequence_length"].as<uint32_t>();
    }
    if (node["bytes_per_parameter"]) {
        spec.bytes_per_parameter = node["bytes_per_parameter"].as<uint32_t>();
    }
    if (node["bytes_per_activation"]) {
        spec.bytes_per_activation =
            node["bytes_per_activation"].as<uint32_t>();
    }
    if (node["bytes_per_kv_element"]) {
        spec.bytes_per_kv_element =
            node["bytes_per_kv_element"].as<uint32_t>();
    }

    if (spec.parameter_count == 0 || spec.num_layers == 0 ||
        spec.hidden_size == 0 || spec.ffn_hidden_size == 0 ||
        spec.attention_heads == 0 || spec.kv_heads == 0 ||
        spec.bytes_per_parameter == 0 || spec.bytes_per_activation == 0 ||
        spec.bytes_per_kv_element == 0) {
        analytical_config_error("Dense model spec is incomplete in " +
                                source_name);
    }
}

DenseModelSpec parse_dense_model(const YAML::Node& node,
                                 const std::string& source_name) {
    DenseModelSpec spec{};
    if (node["preset"]) {
        const auto preset_name = node["preset"].as<std::string>();
        if (preset_name != "70b_dense") {
            analytical_config_error("Unsupported dense model preset '" +
                                    preset_name + "' in " + source_name);
        }
        spec = DenseModelSpec::dense_70b_preset();
    }
    apply_dense_model_overrides(spec, node, source_name);
    return spec;
}

void apply_moe_model_overrides(MoEModelSpec& spec,
                               const YAML::Node& node,
                               const std::string& source_name) {
    if (node["name"]) {
        spec.name = node["name"].as<std::string>();
    }
    if (node["total_parameter_count"]) {
        spec.total_parameter_count =
            node["total_parameter_count"].as<uint64_t>();
    }
    if (node["attention_parameter_count"]) {
        spec.attention_parameter_count =
            node["attention_parameter_count"].as<uint64_t>();
    }
    if (node["router_parameter_count"]) {
        spec.router_parameter_count =
            node["router_parameter_count"].as<uint64_t>();
    }
    if (node["expert_parameter_count"]) {
        spec.expert_parameter_count =
            node["expert_parameter_count"].as<uint64_t>();
    }
    if (node["num_layers"]) {
        spec.num_layers = node["num_layers"].as<uint32_t>();
    }
    if (node["hidden_size"]) {
        spec.hidden_size = node["hidden_size"].as<uint32_t>();
    }
    if (node["attention_heads"]) {
        spec.attention_heads = node["attention_heads"].as<uint32_t>();
    }
    if (node["kv_heads"]) {
        spec.kv_heads = node["kv_heads"].as<uint32_t>();
    }
    if (node["experts_per_layer"]) {
        spec.experts_per_layer = node["experts_per_layer"].as<uint32_t>();
    }
    if (node["active_experts_per_token"]) {
        spec.active_experts_per_token =
            node["active_experts_per_token"].as<uint32_t>();
    }
    if (node["expert_hidden_size"]) {
        spec.expert_hidden_size = node["expert_hidden_size"].as<uint32_t>();
    }
    if (node["max_sequence_length"]) {
        spec.max_sequence_length = node["max_sequence_length"].as<uint32_t>();
    }
    if (node["bytes_per_parameter"]) {
        spec.bytes_per_parameter = node["bytes_per_parameter"].as<uint32_t>();
    }
    if (node["bytes_per_activation"]) {
        spec.bytes_per_activation =
            node["bytes_per_activation"].as<uint32_t>();
    }
    if (node["bytes_per_kv_element"]) {
        spec.bytes_per_kv_element =
            node["bytes_per_kv_element"].as<uint32_t>();
    }

    if (spec.total_parameter_count == 0 || spec.attention_parameter_count == 0 ||
        spec.router_parameter_count == 0 || spec.expert_parameter_count == 0 ||
        spec.num_layers == 0 || spec.hidden_size == 0 ||
        spec.attention_heads == 0 || spec.kv_heads == 0 ||
        spec.experts_per_layer == 0 || spec.active_experts_per_token == 0 ||
        spec.expert_hidden_size == 0 || spec.bytes_per_parameter == 0 ||
        spec.bytes_per_activation == 0 || spec.bytes_per_kv_element == 0) {
        analytical_config_error("MoE model spec is incomplete in " +
                                source_name);
    }

    const auto explicit_total =
        spec.attention_parameter_count + spec.router_parameter_count +
        spec.expert_parameter_count;
    if (explicit_total != spec.total_parameter_count) {
        analytical_config_error("MoE parameter counts must sum to total in " +
                                source_name);
    }
}

MoEModelSpec parse_moe_model(const YAML::Node& node,
                             const std::string& source_name) {
    MoEModelSpec spec{};
    if (node["preset"]) {
        const auto preset_name = node["preset"].as<std::string>();
        if (preset_name != "trillion_parameter_moe") {
            analytical_config_error("Unsupported MoE model preset '" +
                                    preset_name + "' in " + source_name);
        }
        spec = MoEModelSpec::trillion_parameter_moe_preset();
    }
    apply_moe_model_overrides(spec, node, source_name);
    return spec;
}

ClusterSpec parse_cluster(const YAML::Node& node,
                          const std::string& source_name) {
    ClusterSpec cluster{};
    cluster.workspace_reserve_bytes =
        parse_scalar<uint64_t>(node, "workspace_reserve_bytes", source_name);
    if (node["seed"]) {
        cluster.seed = node["seed"].as<uint64_t>();
    }

    const auto devices_node = require_node(node, "devices", source_name);
    if (!devices_node.IsSequence() || devices_node.size() == 0) {
        analytical_config_error(
            "Cluster devices must be a non-empty sequence in " + source_name);
    }

    for (const auto& device_node : devices_node) {
        DeviceSpec device{};
        device.name = parse_scalar<std::string>(device_node, "name", source_name);
        device.type = parse_device_type(
            parse_scalar<std::string>(device_node, "type", source_name),
            source_name);
        device.count = parse_scalar<uint32_t>(device_node, "count", source_name);
        device.memory_capacity_bytes = parse_scalar<uint64_t>(
            device_node, "memory_capacity_bytes", source_name);
        device.peak_flops =
            parse_scalar<double>(device_node, "peak_flops", source_name);
        device.memory_bandwidth_bytes_per_s = parse_scalar<double>(
            device_node, "memory_bandwidth_bytes_per_s", source_name);
        device.active_power_w =
            parse_scalar<double>(device_node, "active_power_w", source_name);
        device.idle_power_w =
            parse_scalar<double>(device_node, "idle_power_w", source_name);
        cluster.devices.push_back(device);
    }

    return cluster;
}

std::vector<InterconnectSpec> parse_interconnects(const YAML::Node& node,
                                                  const std::string& source_name) {
    const auto interconnects_node = require_node(node, "interconnects",
                                                 source_name);
    if (!interconnects_node.IsSequence() || interconnects_node.size() == 0) {
        analytical_config_error("Interconnects must be a non-empty sequence in " +
                                source_name);
    }

    std::vector<InterconnectSpec> interconnects;
    for (const auto& interconnect_node : interconnects_node) {
        InterconnectSpec interconnect{};
        interconnect.name = parse_scalar<std::string>(interconnect_node, "name",
                                                      source_name);
        interconnect.src_type = parse_device_type(
            parse_scalar<std::string>(interconnect_node, "src_type",
                                      source_name),
            source_name);
        interconnect.dst_type = parse_device_type(
            parse_scalar<std::string>(interconnect_node, "dst_type",
                                      source_name),
            source_name);
        interconnect.bandwidth_bytes_per_s = parse_scalar<double>(
            interconnect_node, "bandwidth_bytes_per_s", source_name);
        interconnect.latency_ns = parse_scalar<uint64_t>(interconnect_node,
                                                         "latency_ns",
                                                         source_name);
        interconnect.full_duplex = parse_scalar<bool>(interconnect_node,
                                                      "full_duplex",
                                                      source_name);
        interconnect.efficiency = parse_scalar<double>(interconnect_node,
                                                       "efficiency",
                                                       source_name);
        interconnects.push_back(interconnect);
    }

    return interconnects;
}

AnalyticalOutputPaths parse_outputs(
    const YAML::Node& node,
    const std::string& source_name,
    const std::filesystem::path& base_directory) {
    AnalyticalOutputPaths outputs{};
    outputs.results_csv = resolve_path(
        parse_scalar<std::string>(node, "results_csv", source_name),
        base_directory);
    outputs.summary_json = resolve_path(
        parse_scalar<std::string>(node, "summary_json", source_name),
        base_directory);
    return outputs;
}

TpPpCrossoverConfig parse_tp_pp_crossover_config(
    const YAML::Node& node,
    const std::string& source_name,
    const std::filesystem::path& base_directory) {
    TpPpCrossoverConfig config{};
    config.model = parse_dense_model(require_node(node, "model", source_name),
                                     source_name);
    config.cluster =
        parse_cluster(require_node(node, "cluster", source_name), source_name);
    config.interconnects =
        parse_interconnects(node, source_name);
    config.outputs =
        parse_outputs(require_node(node, "outputs", source_name), source_name,
                      base_directory);
    const auto sweep_node = require_node(node, "sweep", source_name);
    config.sweep.tp_degrees = parse_sorted_list<uint32_t>(
        sweep_node, "tp_degrees", source_name);
    config.sweep.pp_degrees = parse_sorted_list<uint32_t>(
        sweep_node, "pp_degrees", source_name);
    config.sweep.batch_sizes = parse_sorted_list<uint32_t>(
        sweep_node, "batch_sizes", source_name);
    config.sweep.sequence_lengths = parse_sorted_list<uint32_t>(
        sweep_node, "sequence_lengths", source_name);
    if (sweep_node["num_microbatches"]) {
        config.sweep.num_microbatches = parse_sorted_list<uint32_t>(
            sweep_node, "num_microbatches", source_name);
    }
    config.compute_efficiency =
        parse_scalar<double>(node, "compute_efficiency", source_name);
    config.all_reduce_efficiency =
        parse_scalar<double>(node, "all_reduce_efficiency", source_name);
    config.activation_transfer_efficiency = parse_scalar<double>(
        node, "activation_transfer_efficiency", source_name);
    config.activation_multiplier =
        parse_scalar<double>(node, "activation_multiplier", source_name);
    return config;
}

ServingDisaggColocatedConfig parse_serving_disagg_colocated_config(
    const YAML::Node& node,
    const std::string& source_name,
    const std::filesystem::path& base_directory) {
    ServingDisaggColocatedConfig config{};
    config.request_configuration = resolve_path(
        parse_scalar<std::string>(node, "request_configuration", source_name),
        base_directory);
    config.request_metrics_output = resolve_path(
        parse_scalar<std::string>(node, "request_metrics_output", source_name),
        base_directory);
    config.request_summary_output = resolve_path(
        parse_scalar<std::string>(node, "request_summary_output", source_name),
        base_directory);
    config.request_run_metadata_output = resolve_path(
        parse_scalar<std::string>(node, "request_run_metadata_output",
                                  source_name),
        base_directory);
    config.workload_configuration = resolve_path(
        parse_scalar<std::string>(node, "workload_configuration",
                                  source_name),
        base_directory);
    config.comm_group_configuration = resolve_path(
        parse_scalar<std::string>(node, "comm_group_configuration",
                                  source_name),
        base_directory);
    config.system_configuration = resolve_path(
        parse_scalar<std::string>(node, "system_configuration", source_name),
        base_directory);
    config.remote_memory_configuration = resolve_path(
        parse_scalar<std::string>(node, "remote_memory_configuration",
                                  source_name),
        base_directory);
    config.network_configuration = resolve_path(
        parse_scalar<std::string>(node, "network_configuration", source_name),
        base_directory);
    config.logging_configuration = resolve_path(
        parse_scalar<std::string>(node, "logging_configuration", source_name),
        base_directory);
    config.logging_folder = resolve_path(
        parse_scalar<std::string>(node, "logging_folder", source_name),
        base_directory);
    config.num_queues_per_dim =
        parse_scalar<int>(node, "num_queues_per_dim", source_name);
    config.compute_scale =
        parse_scalar<double>(node, "compute_scale", source_name);
    config.comm_scale = parse_scalar<double>(node, "comm_scale", source_name);
    config.injection_scale =
        parse_scalar<double>(node, "injection_scale", source_name);
    config.rendezvous_protocol =
        parse_scalar<bool>(node, "rendezvous_protocol", source_name);
    return config;
}

AttentionFfnDisaggregationConfig parse_attention_ffn_disaggregation_config(
    const YAML::Node& node,
    const std::string& source_name,
    const std::filesystem::path& base_directory) {
    AttentionFfnDisaggregationConfig config{};
    config.model = parse_moe_model(require_node(node, "model", source_name),
                                   source_name);
    config.cluster =
        parse_cluster(require_node(node, "cluster", source_name), source_name);
    config.interconnects =
        parse_interconnects(node, source_name);
    config.outputs =
        parse_outputs(require_node(node, "outputs", source_name), source_name,
                      base_directory);
    config.batch_size =
        parse_scalar<uint32_t>(node, "batch_size", source_name);
    config.sequence_length =
        parse_scalar<uint32_t>(node, "sequence_length", source_name);
    config.attention_compute_efficiency = parse_scalar<double>(
        node, "attention_compute_efficiency", source_name);
    config.router_compute_efficiency = parse_scalar<double>(
        node, "router_compute_efficiency", source_name);
    config.expert_compute_efficiency = parse_scalar<double>(
        node, "expert_compute_efficiency", source_name);
    config.transfer_efficiency = parse_scalar<double>(
        node, "transfer_efficiency", source_name);
    return config;
}

ServingScaleConfig parse_serving_scale_config(
    const YAML::Node& node,
    const std::string& source_name,
    const std::filesystem::path& base_directory) {
    ServingScaleConfig config{};
    const bool has_dense_model = static_cast<bool>(node["dense_model"]);
    const bool has_moe_model = static_cast<bool>(node["moe_model"]);
    if (has_dense_model == has_moe_model) {
        analytical_config_error(
            "serving_scale requires exactly one of dense_model or moe_model in " +
            source_name);
    }
    if (has_dense_model) {
        config.dense_model = parse_dense_model(
            require_node(node, "dense_model", source_name), source_name);
    } else {
        config.moe_model = parse_moe_model(
            require_node(node, "moe_model", source_name), source_name);
    }
    config.cluster =
        parse_cluster(require_node(node, "cluster", source_name), source_name);
    config.interconnects = parse_interconnects(node, source_name);
    config.topology = parse_serving_topology(
        require_node(node, "topology", source_name), source_name);
    config.request_configuration = resolve_path(
        parse_scalar<std::string>(node, "request_configuration", source_name),
        base_directory);
    config.request_metrics_output = resolve_path(
        parse_scalar<std::string>(node, "request_metrics_output", source_name),
        base_directory);
    config.request_summary_output = resolve_path(
        parse_scalar<std::string>(node, "request_summary_output", source_name),
        base_directory);
    config.request_run_metadata_output = resolve_path(
        parse_scalar<std::string>(node, "request_run_metadata_output",
                                  source_name),
        base_directory);
    config.workload_configuration = resolve_path(
        parse_scalar<std::string>(node, "workload_configuration", source_name),
        base_directory);
    config.comm_group_configuration = resolve_path(
        parse_scalar<std::string>(node, "comm_group_configuration",
                                  source_name),
        base_directory);
    config.system_configuration = resolve_path(
        parse_scalar<std::string>(node, "system_configuration", source_name),
        base_directory);
    config.remote_memory_configuration = resolve_path(
        parse_scalar<std::string>(node, "remote_memory_configuration",
                                  source_name),
        base_directory);
    config.network_configuration = resolve_path(
        parse_scalar<std::string>(node, "network_configuration", source_name),
        base_directory);
    config.logging_configuration = resolve_path(
        parse_scalar<std::string>(node, "logging_configuration", source_name),
        base_directory);
    config.logging_folder = resolve_path(
        parse_scalar<std::string>(node, "logging_folder", source_name),
        base_directory);
    config.num_queues_per_dim =
        parse_scalar<int>(node, "num_queues_per_dim", source_name);
    config.compute_scale =
        parse_scalar<double>(node, "compute_scale", source_name);
    config.comm_scale = parse_scalar<double>(node, "comm_scale", source_name);
    config.injection_scale =
        parse_scalar<double>(node, "injection_scale", source_name);
    config.rendezvous_protocol =
        parse_scalar<bool>(node, "rendezvous_protocol", source_name);
    return config;
}

AnalyticalConfig parse_analytical_config(const YAML::Node& root,
                                         const std::string& source_name) {
    if (!root.IsMap()) {
        analytical_config_error("Analytical config root must be a mapping in " +
                                source_name);
    }

    AnalyticalConfig config{};
    config.source_path = source_name;
    config.mode = parse_analytical_mode(
        parse_scalar<std::string>(root, "mode", source_name),
        source_name);
    const auto base_directory = base_directory_for_source(source_name);

    switch (config.mode) {
    case AnalyticalMode::tp_pp_crossover:
        config.tp_pp_crossover = parse_tp_pp_crossover_config(
            root, source_name, base_directory);
        break;
    case AnalyticalMode::serving_disagg_colocated:
        config.serving_disagg_colocated =
            parse_serving_disagg_colocated_config(root, source_name,
                                                  base_directory);
        break;
    case AnalyticalMode::attention_ffn_disaggregation:
        config.attention_ffn_disaggregation =
            parse_attention_ffn_disaggregation_config(root, source_name,
                                                      base_directory);
        break;
    case AnalyticalMode::serving_scale:
        config.serving_scale =
            parse_serving_scale_config(root, source_name, base_directory);
        break;
    }

    return config;
}

}  // namespace

DenseModelSpec DenseModelSpec::dense_70b_preset() {
    return DenseModelSpec{
        "70B Dense",
        70000000000ULL,
        80,
        8192,
        28672,
        64,
        8,
        8192,
        2,
        2,
        2,
    };
}

MoEModelSpec MoEModelSpec::trillion_parameter_moe_preset() {
    return MoEModelSpec{
        "1T MoE",
        1000000000000ULL,
        180000000000ULL,
        20000000000ULL,
        800000000000ULL,
        64,
        12288,
        96,
        12,
        64,
        2,
        49152,
        16384,
        2,
        2,
        2,
    };
}

const DeviceSpec* ClusterSpec::find_device(DeviceType type) const {
    for (const auto& device : devices) {
        if (device.type == type) {
            return &device;
        }
    }
    return nullptr;
}

AnalyticalConfig AnalyticalConfig::load_from_file(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        analytical_config_error("Unable to open analytical config file: " +
                                path);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return load_from_yaml_text(buffer.str(), path);
}

AnalyticalConfig AnalyticalConfig::load_from_yaml_text(
    const std::string& yaml_text,
    const std::string& source_name) {
    try {
        return parse_analytical_config(YAML::Load(yaml_text), source_name);
    } catch (const YAML::Exception& error) {
        analytical_config_error("Failed to parse analytical config " +
                                source_name + ": " + error.what());
    }
}

std::string to_string(AnalyticalMode mode) {
    switch (mode) {
    case AnalyticalMode::tp_pp_crossover:
        return "tp_pp_crossover";
    case AnalyticalMode::serving_disagg_colocated:
        return "serving_disagg_colocated";
    case AnalyticalMode::attention_ffn_disaggregation:
        return "attention_ffn_disaggregation";
    case AnalyticalMode::serving_scale:
        return "serving_scale";
    }
    return "unknown";
}

std::string to_string(DeviceType type) {
    switch (type) {
    case DeviceType::GPU:
        return "GPU";
    case DeviceType::LPU:
        return "LPU";
    }
    return "Unknown";
}

}  // namespace AstraSim
