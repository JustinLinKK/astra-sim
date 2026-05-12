#include "astra-sim/workload/ServingConfig.hh"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <json/json.hpp>
#include <limits>
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

bool is_path_like_source(const std::string& source_name) {
    return !source_name.empty() && source_name.front() != '<';
}

std::string resolve_optional_path(const std::string& value,
                                  const std::string& source_name) {
    if (value.empty() || value == "empty" || !is_path_like_source(source_name)) {
        return value;
    }
    const auto base_path = std::filesystem::absolute(source_name).parent_path();
    return std::filesystem::absolute(base_path / value).string();
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

void require_boolean(const json& parent,
                     const std::string& field_name,
                     const std::string& source_name) {
    if (!parent.contains(field_name) || !parent[field_name].is_boolean()) {
        serving_config_error("Serving config field '" + field_name +
                             "' must be a boolean in " + source_name);
    }
}

ComType parse_collective_type(const std::string& type_name,
                              const std::string& field_name,
                              const std::string& source_name) {
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
                         "' in field '" + field_name + "' from " +
                         source_name);
}

ServingCollectiveSpec parse_collective(const json& parent,
                                       const std::string& field_name,
                                       const std::string& source_name,
                                       ServingCollectiveSpec default_value =
                                           ServingCollectiveSpec{}) {
    if (!parent.contains(field_name)) {
        return default_value;
    }
    if (!parent[field_name].is_object()) {
        serving_config_error("Serving config field '" + field_name +
                             "' must be an object in " + source_name);
    }
    const auto& collective_json = parent[field_name];
    ServingCollectiveSpec collective = default_value;

    if (collective_json.contains("enabled")) {
        require_boolean(collective_json, "enabled",
                        source_name + " (" + field_name + ")");
        collective.enabled = collective_json["enabled"].get<bool>();
    }
    if (collective_json.contains("type")) {
        require_string(collective_json, "type",
                       source_name + " (" + field_name + ")");
        collective.type_name = collective_json["type"].get<std::string>();
        collective.type =
            parse_collective_type(collective.type_name, field_name, source_name);
    } else if (collective.enabled) {
        serving_config_error("Serving collective '" + field_name +
                             "' must set 'type' when enabled in " +
                             source_name);
    }
    if (collective_json.contains("size_bytes")) {
        require_unsigned_integer(collective_json, "size_bytes",
                                 source_name + " (" + field_name + ")");
        collective.size_bytes = collective_json["size_bytes"].get<uint64_t>();
    } else if (collective.enabled) {
        serving_config_error("Serving collective '" + field_name +
                             "' must set 'size_bytes' when enabled in " +
                             source_name);
    }

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

ServingArchitecture parse_architecture(const std::string& value,
                                       const std::string& source_name) {
    if (value == "serial_baseline") {
        return ServingArchitecture::SerialBaseline;
    }
    if (value == "colocated") {
        return ServingArchitecture::Colocated;
    }
    if (value == "colocated_chunked") {
        return ServingArchitecture::ColocatedChunked;
    }
    if (value == "pd_disaggregated") {
        return ServingArchitecture::PdDisaggregated;
    }
    serving_config_error("Unsupported serving architecture '" + value +
                         "' in " + source_name);
}

ServingTopologyDeployment parse_topology_deployment(
    const std::string& value,
    const std::string& source_name) {
    if (value == "colocated") {
        return ServingTopologyDeployment::Colocated;
    }
    if (value == "prefill_decode_disaggregated" ||
        value == "pd_disaggregated") {
        return ServingTopologyDeployment::PrefillDecodeDisaggregated;
    }
    serving_config_error("Unsupported serving topology deployment '" + value +
                         "' in " + source_name);
}

ServingSchedulerPolicy parse_scheduler_policy(const std::string& value,
                                              const std::string& source_name) {
    if (value == "serial") {
        return ServingSchedulerPolicy::Serial;
    }
    if (value == "decode_first") {
        return ServingSchedulerPolicy::DecodeFirst;
    }
    if (value == "prefill_first") {
        return ServingSchedulerPolicy::PrefillFirst;
    }
    if (value == "balanced") {
        return ServingSchedulerPolicy::Balanced;
    }
    serving_config_error("Unsupported serving scheduler policy '" + value +
                         "' in " + source_name);
}

ServingRuntimeConfig parse_runtime(const json& root,
                                   const std::string& source_name) {
    ServingRuntimeConfig runtime;
    if (!root.contains("runtime")) {
        return runtime;
    }
    require_object(root, "runtime", source_name);
    const auto& runtime_json = root["runtime"];
    if (runtime_json.contains("architecture")) {
        require_string(runtime_json, "architecture", source_name);
        runtime.architecture = parse_architecture(
            runtime_json["architecture"].get<std::string>(), source_name);
    }
    if (runtime_json.contains("seed")) {
        require_unsigned_integer(runtime_json, "seed", source_name);
        runtime.seed = runtime_json["seed"].get<uint64_t>();
    }
    return runtime;
}

ParallelismLayoutSpec parse_parallelism_layout(
    const json& parent,
    const std::string& field_name,
    const std::string& source_name,
    ParallelismLayoutSpec default_value = ParallelismLayoutSpec{}) {
    if (!parent.contains(field_name)) {
        return default_value;
    }
    require_object(parent, field_name, source_name);
    const auto& layout_json = parent[field_name];
    auto layout = default_value;
    if (layout_json.contains("name")) {
        require_string(layout_json, "name", source_name);
        layout.name = layout_json["name"].get<std::string>();
    }
    if (layout_json.contains("tp_degree")) {
        require_unsigned_integer(layout_json, "tp_degree", source_name);
        layout.tp_degree = layout_json["tp_degree"].get<uint64_t>();
    }
    if (layout_json.contains("pp_degree")) {
        require_unsigned_integer(layout_json, "pp_degree", source_name);
        layout.pp_degree = layout_json["pp_degree"].get<uint64_t>();
    }
    if (layout_json.contains("ep_degree")) {
        require_unsigned_integer(layout_json, "ep_degree", source_name);
        layout.ep_degree = layout_json["ep_degree"].get<uint64_t>();
    }
    if (layout_json.contains("dp_attention_degree")) {
        require_unsigned_integer(layout_json, "dp_attention_degree",
                                 source_name);
        layout.dp_attention_degree =
            layout_json["dp_attention_degree"].get<uint64_t>();
    }
    if (layout_json.contains("dp_replica_count")) {
        require_unsigned_integer(layout_json, "dp_replica_count", source_name);
        layout.dp_replica_count =
            layout_json["dp_replica_count"].get<uint64_t>();
    }
    return layout;
}

std::optional<ServingTopologySpec> parse_topology(const json& root,
                                                  const std::string& source_name) {
    if (!root.contains("topology")) {
        return std::nullopt;
    }
    require_object(root, "topology", source_name);
    const auto& topology_json = root["topology"];

    ServingTopologySpec topology;
    if (topology_json.contains("deployment")) {
        require_string(topology_json, "deployment", source_name);
        topology.deployment = parse_topology_deployment(
            topology_json["deployment"].get<std::string>(), source_name);
    }
    topology.colocated_layout = parse_parallelism_layout(
        topology_json, "colocated_layout", source_name,
        ParallelismLayoutSpec{"colocated", 1, 1, 1, 1, 1});
    if (topology_json.contains("prefill_layout")) {
        topology.prefill_layout = parse_parallelism_layout(
            topology_json, "prefill_layout", source_name,
            ParallelismLayoutSpec{"prefill", 1, 1, 1, 1, 1});
    }
    if (topology_json.contains("decode_layout")) {
        topology.decode_layout = parse_parallelism_layout(
            topology_json, "decode_layout", source_name,
            ParallelismLayoutSpec{"decode", 1, 1, 1, 1, 1});
    }
    return topology;
}

ServingSchedulerConfig parse_scheduler(const json& root,
                                       const std::string& source_name) {
    ServingSchedulerConfig scheduler;
    scheduler.max_prefill_batch_tokens = 0;
    if (!root.contains("scheduler")) {
        return scheduler;
    }
    require_object(root, "scheduler", source_name);
    const auto& scheduler_json = root["scheduler"];
    if (scheduler_json.contains("max_running_requests")) {
        require_unsigned_integer(scheduler_json, "max_running_requests",
                                 source_name);
        scheduler.max_running_requests =
            scheduler_json["max_running_requests"].get<uint64_t>();
    }
    if (scheduler_json.contains("scheduler_policy")) {
        require_string(scheduler_json, "scheduler_policy", source_name);
        scheduler.scheduler_policy = parse_scheduler_policy(
            scheduler_json["scheduler_policy"].get<std::string>(), source_name);
    }
    if (scheduler_json.contains("max_prefill_batch_tokens")) {
        require_unsigned_integer(scheduler_json, "max_prefill_batch_tokens",
                                 source_name);
        scheduler.max_prefill_batch_tokens =
            scheduler_json["max_prefill_batch_tokens"].get<uint64_t>();
    }
    if (scheduler_json.contains("max_decode_batch_requests")) {
        require_unsigned_integer(scheduler_json, "max_decode_batch_requests",
                                 source_name);
        scheduler.max_decode_batch_requests =
            scheduler_json["max_decode_batch_requests"].get<uint64_t>();
    }
    if (scheduler_json.contains("chunked_prefill_size")) {
        require_unsigned_integer(scheduler_json, "chunked_prefill_size",
                                 source_name);
        scheduler.chunked_prefill_size =
            scheduler_json["chunked_prefill_size"].get<uint64_t>();
    }
    if (scheduler_json.contains("prefill_max_requests")) {
        require_unsigned_integer(scheduler_json, "prefill_max_requests",
                                 source_name);
        scheduler.prefill_max_requests =
            scheduler_json["prefill_max_requests"].get<uint64_t>();
    }
    if (scheduler_json.contains("enable_mixed_chunk")) {
        require_boolean(scheduler_json, "enable_mixed_chunk", source_name);
        scheduler.enable_mixed_chunk =
            scheduler_json["enable_mixed_chunk"].get<bool>();
    }
    return scheduler;
}

ServingSloConfig parse_slo(const json& root, const std::string& source_name) {
    ServingSloConfig slo;
    if (!root.contains("slo")) {
        return slo;
    }
    require_object(root, "slo", source_name);
    const auto& slo_json = root["slo"];
    if (slo_json.contains("ttft_ns")) {
        require_unsigned_integer(slo_json, "ttft_ns", source_name);
        slo.ttft_ns = slo_json["ttft_ns"].get<uint64_t>();
        slo.enabled = true;
    }
    if (slo_json.contains("tpot_ns")) {
        require_number(slo_json, "tpot_ns", source_name);
        slo.tpot_ns = slo_json["tpot_ns"].get<double>();
        slo.enabled = true;
    }
    if (slo_json.contains("e2e_ns")) {
        require_unsigned_integer(slo_json, "e2e_ns", source_name);
        slo.e2e_ns = slo_json["e2e_ns"].get<uint64_t>();
        slo.enabled = true;
    }
    return slo;
}

std::optional<ServingModelConfig> parse_model(const json& root,
                                              const std::string& source_name) {
    if (!root.contains("model")) {
        return std::nullopt;
    }
    require_object(root, "model", source_name);
    const auto& model_json = root["model"];

    ServingModelConfig model;
    if (model_json.contains("name")) {
        require_string(model_json, "name", source_name);
        model.name = model_json["name"].get<std::string>();
    }
    if (model_json.contains("kind")) {
        require_string(model_json, "kind", source_name);
        model.kind = model_json["kind"].get<std::string>();
    }
    require_unsigned_integer(model_json, "num_layers", source_name);
    require_unsigned_integer(model_json, "hidden_size", source_name);
    require_unsigned_integer(model_json, "attention_heads", source_name);
    require_unsigned_integer(model_json, "kv_heads", source_name);
    require_unsigned_integer(model_json, "head_dim", source_name);
    require_unsigned_integer(model_json, "bytes_per_kv_element", source_name);
    model.num_layers = model_json["num_layers"].get<uint64_t>();
    model.hidden_size = model_json["hidden_size"].get<uint64_t>();
    if (model_json.contains("ffn_hidden_size")) {
        require_unsigned_integer(model_json, "ffn_hidden_size", source_name);
        model.ffn_hidden_size =
            model_json["ffn_hidden_size"].get<uint64_t>();
    }
    model.attention_heads = model_json["attention_heads"].get<uint64_t>();
    model.kv_heads = model_json["kv_heads"].get<uint64_t>();
    model.head_dim = model_json["head_dim"].get<uint64_t>();
    if (model_json.contains("experts_per_layer")) {
        require_unsigned_integer(model_json, "experts_per_layer", source_name);
        model.experts_per_layer =
            model_json["experts_per_layer"].get<uint64_t>();
    }
    if (model_json.contains("active_experts_per_token")) {
        require_unsigned_integer(model_json, "active_experts_per_token",
                                 source_name);
        model.active_experts_per_token =
            model_json["active_experts_per_token"].get<uint64_t>();
    }
    if (model_json.contains("expert_hidden_size")) {
        require_unsigned_integer(model_json, "expert_hidden_size",
                                 source_name);
        model.expert_hidden_size =
            model_json["expert_hidden_size"].get<uint64_t>();
    }
    if (model_json.contains("bytes_per_parameter")) {
        require_unsigned_integer(model_json, "bytes_per_parameter",
                                 source_name);
        model.bytes_per_parameter =
            model_json["bytes_per_parameter"].get<uint64_t>();
    }
    if (model_json.contains("bytes_per_activation")) {
        require_unsigned_integer(model_json, "bytes_per_activation",
                                 source_name);
        model.bytes_per_activation =
            model_json["bytes_per_activation"].get<uint64_t>();
    }
    model.bytes_per_kv_element =
        model_json["bytes_per_kv_element"].get<uint64_t>();
    return model;
}

ServingTransferConfig parse_transfer(const json& parent,
                                     const std::string& source_name) {
    ServingTransferConfig transfer;
    if (!parent.contains("transfer")) {
        return transfer;
    }
    if (!parent["transfer"].is_object()) {
        serving_config_error("Serving config field 'pd.transfer' must be an object in " +
                             source_name);
    }
    const auto& transfer_json = parent["transfer"];
    if (transfer_json.contains("enabled")) {
        require_boolean(transfer_json, "enabled", source_name);
        transfer.enabled = transfer_json["enabled"].get<bool>();
    }
    if (transfer_json.contains("latency_ns")) {
        require_unsigned_integer(transfer_json, "latency_ns", source_name);
        transfer.latency_ns = transfer_json["latency_ns"].get<uint64_t>();
    }
    if (transfer_json.contains("bandwidth_bytes_per_s")) {
        require_number(transfer_json, "bandwidth_bytes_per_s", source_name);
        transfer.bandwidth_bytes_per_s =
            transfer_json["bandwidth_bytes_per_s"].get<double>();
    }
    if (transfer_json.contains("efficiency")) {
        require_number(transfer_json, "efficiency", source_name);
        transfer.efficiency = transfer_json["efficiency"].get<double>();
    }
    if (transfer_json.contains("overlap_enabled")) {
        require_boolean(transfer_json, "overlap_enabled", source_name);
        transfer.overlap_enabled =
            transfer_json["overlap_enabled"].get<bool>();
    }
    if (transfer_json.contains("bytes_per_prompt_token")) {
        require_unsigned_integer(transfer_json, "bytes_per_prompt_token",
                                 source_name);
        transfer.bytes_per_prompt_token =
            transfer_json["bytes_per_prompt_token"].get<uint64_t>();
    }
    if (transfer_json.contains("override_bytes_per_prompt_token")) {
        require_boolean(transfer_json, "override_bytes_per_prompt_token",
                        source_name);
        transfer.override_bytes_per_prompt_token =
            transfer_json["override_bytes_per_prompt_token"].get<bool>();
    }
    return transfer;
}

std::optional<ServingPdConfig> parse_pd(const json& root,
                                        const std::string& source_name) {
    if (!root.contains("pd")) {
        return std::nullopt;
    }
    require_object(root, "pd", source_name);
    const auto& pd_json = root["pd"];
    ServingPdConfig pd;

    if (pd_json.contains("prefill_workers")) {
        require_unsigned_integer(pd_json, "prefill_workers", source_name);
        pd.prefill_workers = pd_json["prefill_workers"].get<uint64_t>();
    }
    if (pd_json.contains("decode_workers")) {
        require_unsigned_integer(pd_json, "decode_workers", source_name);
        pd.decode_workers = pd_json["decode_workers"].get<uint64_t>();
    }
    if (pd_json.contains("prefill_max_batch_tokens")) {
        require_unsigned_integer(pd_json, "prefill_max_batch_tokens",
                                 source_name);
        pd.prefill_max_batch_tokens =
            pd_json["prefill_max_batch_tokens"].get<uint64_t>();
    }
    if (pd_json.contains("prefill_max_requests")) {
        require_unsigned_integer(pd_json, "prefill_max_requests", source_name);
        pd.prefill_max_requests =
            pd_json["prefill_max_requests"].get<uint64_t>();
    }
    if (pd_json.contains("decode_max_batch_requests")) {
        require_unsigned_integer(pd_json, "decode_max_batch_requests",
                                 source_name);
        pd.decode_max_batch_requests =
            pd_json["decode_max_batch_requests"].get<uint64_t>();
    }
    if (pd_json.contains("prefill_tp_degree")) {
        require_unsigned_integer(pd_json, "prefill_tp_degree", source_name);
        pd.prefill_tp_degree =
            pd_json["prefill_tp_degree"].get<uint64_t>();
    }
    if (pd_json.contains("decode_tp_degree")) {
        require_unsigned_integer(pd_json, "decode_tp_degree", source_name);
        pd.decode_tp_degree = pd_json["decode_tp_degree"].get<uint64_t>();
    }
    pd.transfer = parse_transfer(pd_json, source_name);
    return pd;
}

ServingOutputsConfig parse_outputs(const json& root,
                                   const std::string& source_name) {
    ServingOutputsConfig outputs;
    if (!root.contains("outputs")) {
        return outputs;
    }
    require_object(root, "outputs", source_name);
    const auto& outputs_json = root["outputs"];
    if (outputs_json.contains("event_trace_output")) {
        require_string(outputs_json, "event_trace_output", source_name);
        outputs.event_trace_output = resolve_optional_path(
            outputs_json["event_trace_output"].get<std::string>(), source_name);
    }
    return outputs;
}

ServingCostModelConfig parse_cost_model(const json& root,
                                        const std::string& source_name,
                                        ServingCollectiveSpec* prefill_collective,
                                        ServingCollectiveSpec* decode_collective) {
    const bool has_baseline = root.contains("baseline");
    const bool has_cost_model = root.contains("cost_model");
    if (!has_baseline && !has_cost_model) {
        serving_config_error(
            "Serving config must contain at least one of 'baseline' or 'cost_model' in " +
            source_name);
    }

    ServingCostModelConfig cost_model;
    auto parse_stage_components =
        [&](const json& parent,
            const std::string& field_name,
            ServingCostModelConfig::StageComponentConfig* stage) {
            if (!parent.contains(field_name)) {
                return;
            }
            require_object(parent, field_name, source_name);
            const auto& stage_json = parent[field_name];
            if (stage_json.contains("base_latency_ns")) {
                require_unsigned_integer(stage_json, "base_latency_ns",
                                         source_name);
                stage->base_latency_ns =
                    stage_json["base_latency_ns"].get<uint64_t>();
            }
            if (stage_json.contains("attention_compute_ns_per_token")) {
                require_unsigned_integer(stage_json,
                                         "attention_compute_ns_per_token",
                                         source_name);
                stage->attention_compute_ns_per_token =
                    stage_json["attention_compute_ns_per_token"].get<uint64_t>();
            }
            if (stage_json.contains("ffn_or_expert_compute_ns_per_token")) {
                require_unsigned_integer(stage_json,
                                         "ffn_or_expert_compute_ns_per_token",
                                         source_name);
                stage->ffn_or_expert_compute_ns_per_token =
                    stage_json["ffn_or_expert_compute_ns_per_token"]
                        .get<uint64_t>();
            }
            if (stage_json.contains("tp_collective_ns_per_token")) {
                require_unsigned_integer(stage_json,
                                         "tp_collective_ns_per_token",
                                         source_name);
                stage->tp_collective_ns_per_token =
                    stage_json["tp_collective_ns_per_token"].get<uint64_t>();
            }
            if (stage_json.contains("pp_activation_ns_per_token")) {
                require_unsigned_integer(stage_json,
                                         "pp_activation_ns_per_token",
                                         source_name);
                stage->pp_activation_ns_per_token =
                    stage_json["pp_activation_ns_per_token"].get<uint64_t>();
            }
            if (stage_json.contains("ep_dispatch_ns_per_token")) {
                require_unsigned_integer(stage_json,
                                         "ep_dispatch_ns_per_token",
                                         source_name);
                stage->ep_dispatch_ns_per_token =
                    stage_json["ep_dispatch_ns_per_token"].get<uint64_t>();
            }
            if (stage_json.contains("dp_attention_sync_ns_per_token")) {
                require_unsigned_integer(stage_json,
                                         "dp_attention_sync_ns_per_token",
                                         source_name);
                stage->dp_attention_sync_ns_per_token =
                    stage_json["dp_attention_sync_ns_per_token"]
                        .get<uint64_t>();
            }
            if (stage_json.contains("batch_efficiency")) {
                require_number(stage_json, "batch_efficiency", source_name);
                stage->batch_efficiency =
                    stage_json["batch_efficiency"].get<double>();
            }
            if (stage_json.contains("interference_factor")) {
                require_number(stage_json, "interference_factor", source_name);
                stage->interference_factor =
                    stage_json["interference_factor"].get<double>();
            }
        };
    if (has_baseline) {
        require_object(root, "baseline", source_name);
        const auto& baseline = root["baseline"];
        require_unsigned_integer(baseline, "prefill_base_latency_ns", source_name);
        require_unsigned_integer(baseline, "prefill_compute_ns_per_token",
                                 source_name);
        require_unsigned_integer(baseline, "decode_base_latency_ns", source_name);
        require_unsigned_integer(baseline, "decode_compute_ns_per_token",
                                 source_name);
        cost_model.prefill_base_latency_ns =
            baseline["prefill_base_latency_ns"].get<uint64_t>();
        cost_model.prefill_compute_ns_per_token =
            baseline["prefill_compute_ns_per_token"].get<uint64_t>();
        cost_model.decode_base_latency_ns =
            baseline["decode_base_latency_ns"].get<uint64_t>();
        cost_model.decode_compute_ns_per_token =
            baseline["decode_compute_ns_per_token"].get<uint64_t>();
        *prefill_collective = parse_collective(
            baseline, "prefill_collective", source_name,
            ServingCollectiveSpec{false, ComType::All_Reduce, "all-reduce", 0});
        *decode_collective = parse_collective(
            baseline, "decode_collective", source_name,
            ServingCollectiveSpec{false, ComType::All_Reduce, "all-reduce", 0});
    } else {
        *prefill_collective =
            ServingCollectiveSpec{false, ComType::All_Reduce, "all-reduce", 0};
        *decode_collective =
            ServingCollectiveSpec{false, ComType::All_Reduce, "all-reduce", 0};
    }

    if (has_cost_model) {
        require_object(root, "cost_model", source_name);
        const auto& model_json = root["cost_model"];
        if (model_json.contains("prefill_base_latency_ns")) {
            require_unsigned_integer(model_json, "prefill_base_latency_ns",
                                     source_name);
            cost_model.prefill_base_latency_ns =
                model_json["prefill_base_latency_ns"].get<uint64_t>();
        }
        if (model_json.contains("prefill_ns_per_token")) {
            require_unsigned_integer(model_json, "prefill_ns_per_token",
                                     source_name);
            cost_model.prefill_compute_ns_per_token =
                model_json["prefill_ns_per_token"].get<uint64_t>();
        }
        if (model_json.contains("decode_base_latency_ns")) {
            require_unsigned_integer(model_json, "decode_base_latency_ns",
                                     source_name);
            cost_model.decode_base_latency_ns =
                model_json["decode_base_latency_ns"].get<uint64_t>();
        }
        if (model_json.contains("decode_ns_per_token")) {
            require_unsigned_integer(model_json, "decode_ns_per_token",
                                     source_name);
            cost_model.decode_compute_ns_per_token =
                model_json["decode_ns_per_token"].get<uint64_t>();
        }
        if (model_json.contains("prefill_batch_efficiency")) {
            require_number(model_json, "prefill_batch_efficiency", source_name);
            cost_model.prefill_batch_efficiency =
                model_json["prefill_batch_efficiency"].get<double>();
        }
        if (model_json.contains("decode_batch_efficiency")) {
            require_number(model_json, "decode_batch_efficiency", source_name);
            cost_model.decode_batch_efficiency =
                model_json["decode_batch_efficiency"].get<double>();
        }
        if (model_json.contains("decode_interference_factor")) {
            require_number(model_json, "decode_interference_factor",
                           source_name);
            cost_model.decode_interference_factor =
                model_json["decode_interference_factor"].get<double>();
        }
        if (model_json.contains("mixed_prefill_weight")) {
            require_number(model_json, "mixed_prefill_weight", source_name);
            cost_model.mixed_prefill_weight =
                model_json["mixed_prefill_weight"].get<double>();
        }
        parse_stage_components(model_json, "prefill", &cost_model.prefill);
        parse_stage_components(model_json, "decode", &cost_model.decode);
        cost_model.topology_aware_enabled =
            model_json.contains("prefill") || model_json.contains("decode");
    }

    if (cost_model.prefill.base_latency_ns == 0) {
        cost_model.prefill.base_latency_ns = cost_model.prefill_base_latency_ns;
    }
    if (cost_model.decode.base_latency_ns == 0) {
        cost_model.decode.base_latency_ns = cost_model.decode_base_latency_ns;
    }
    if (cost_model.prefill.attention_compute_ns_per_token == 0 &&
        cost_model.prefill.ffn_or_expert_compute_ns_per_token == 0) {
        cost_model.prefill.attention_compute_ns_per_token =
            cost_model.prefill_compute_ns_per_token;
    }
    if (cost_model.decode.attention_compute_ns_per_token == 0 &&
        cost_model.decode.ffn_or_expert_compute_ns_per_token == 0 &&
        cost_model.decode.tp_collective_ns_per_token == 0 &&
        cost_model.decode.pp_activation_ns_per_token == 0 &&
        cost_model.decode.ep_dispatch_ns_per_token == 0 &&
        cost_model.decode.dp_attention_sync_ns_per_token == 0) {
        cost_model.decode.attention_compute_ns_per_token =
            cost_model.decode_compute_ns_per_token;
    }
    if (cost_model.prefill.batch_efficiency == 1.0) {
        cost_model.prefill.batch_efficiency =
            cost_model.prefill_batch_efficiency;
    }
    if (cost_model.decode.batch_efficiency == 1.0) {
        cost_model.decode.batch_efficiency =
            cost_model.decode_batch_efficiency;
    }
    if (cost_model.decode.interference_factor == 1.0) {
        cost_model.decode.interference_factor =
            cost_model.decode_interference_factor;
    }

    cost_model.prefill_base_latency_ns = cost_model.prefill.base_latency_ns;
    cost_model.decode_base_latency_ns = cost_model.decode.base_latency_ns;
    cost_model.prefill_compute_ns_per_token =
        cost_model.prefill.attention_compute_ns_per_token +
        cost_model.prefill.ffn_or_expert_compute_ns_per_token +
        cost_model.prefill.tp_collective_ns_per_token +
        cost_model.prefill.pp_activation_ns_per_token +
        cost_model.prefill.ep_dispatch_ns_per_token +
        cost_model.prefill.dp_attention_sync_ns_per_token;
    cost_model.decode_compute_ns_per_token =
        cost_model.decode.attention_compute_ns_per_token +
        cost_model.decode.ffn_or_expert_compute_ns_per_token +
        cost_model.decode.tp_collective_ns_per_token +
        cost_model.decode.pp_activation_ns_per_token +
        cost_model.decode.ep_dispatch_ns_per_token +
        cost_model.decode.dp_attention_sync_ns_per_token;
    cost_model.prefill_batch_efficiency = cost_model.prefill.batch_efficiency;
    cost_model.decode_batch_efficiency = cost_model.decode.batch_efficiency;
    cost_model.decode_interference_factor =
        cost_model.decode.interference_factor;

    return cost_model;
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
        if (prompt_tokens == 0 || output_tokens == 0) {
            serving_config_error(
                "Serving request prompt/output token counts must be greater than zero in " +
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

void validate_config(const ServingConfig& config,
                     const std::string& source_name) {
    const auto validate_layout =
        [&](const ParallelismLayoutSpec& layout,
            const std::string& layout_name) {
            if (layout.tp_degree < 1 || layout.pp_degree < 1 ||
                layout.ep_degree < 1 || layout.dp_attention_degree < 1 ||
                layout.dp_replica_count < 1) {
                serving_config_error("Serving topology layout '" + layout_name +
                                     "' must use positive degrees in " +
                                     source_name);
            }
        };

    if (config.scheduler.max_running_requests < 1) {
        serving_config_error("Serving scheduler max_running_requests must be >= 1 in " +
                             source_name);
    }
    if (config.scheduler.max_decode_batch_requests < 1) {
        serving_config_error(
            "Serving scheduler max_decode_batch_requests must be >= 1 in " +
            source_name);
    }
    if (config.scheduler.prefill_max_requests < 1) {
        serving_config_error(
            "Serving scheduler prefill_max_requests must be >= 1 in " +
            source_name);
    }
    if (config.runtime.architecture ==
            ServingArchitecture::ColocatedChunked &&
        config.scheduler.chunked_prefill_size == 0) {
        serving_config_error(
            "Serving architecture 'colocated_chunked' requires chunked_prefill_size > 0 in " +
            source_name);
    }
    if (config.runtime.architecture !=
            ServingArchitecture::ColocatedChunked &&
        config.scheduler.chunked_prefill_size == 0) {
        // allowed
    } else if (config.runtime.architecture !=
                   ServingArchitecture::ColocatedChunked &&
               config.scheduler.chunked_prefill_size > 0 &&
               config.runtime.architecture ==
                   ServingArchitecture::SerialBaseline) {
        serving_config_error(
            "chunked_prefill_size > 0 is not supported for serial_baseline in " +
            source_name);
    }
    if (config.slo.enabled) {
        if (config.slo.ttft_ns == 0 && config.slo.tpot_ns <= 0.0 &&
            !config.slo.e2e_ns.has_value()) {
            serving_config_error(
                "Serving SLO config must include at least one positive threshold in " +
                source_name);
        }
        if (config.slo.ttft_ns == 0 && config.slo.tpot_ns > 0.0) {
            // allowed
        }
        if (config.slo.ttft_ns > 0 && config.slo.ttft_ns < 1) {
            serving_config_error("Serving SLO ttft_ns must be positive in " +
                                 source_name);
        }
        if (config.slo.tpot_ns < 0.0) {
            serving_config_error("Serving SLO tpot_ns must be positive in " +
                                 source_name);
        }
        if (config.slo.enabled && config.slo.tpot_ns == 0.0 &&
            config.slo.ttft_ns == 0 && !config.slo.e2e_ns.has_value()) {
            serving_config_error("Serving SLO thresholds must be positive in " +
                                 source_name);
        }
        if (config.slo.enabled && config.slo.tpot_ns == 0.0 &&
            (config.slo.ttft_ns > 0 || config.slo.e2e_ns.has_value())) {
            // allowed
        }
        if (config.slo.e2e_ns.has_value() && *config.slo.e2e_ns == 0) {
            serving_config_error("Serving SLO e2e_ns must be positive in " +
                                 source_name);
        }
    }
    if (config.runtime.architecture == ServingArchitecture::PdDisaggregated) {
        if (!config.pd.has_value()) {
            serving_config_error(
                "Serving architecture 'pd_disaggregated' requires a 'pd' section in " +
                source_name);
        }
        if (config.pd->prefill_workers < 1 || config.pd->decode_workers < 1) {
            serving_config_error(
                "PD serving config requires prefill_workers >= 1 and decode_workers >= 1 in " +
                source_name);
        }
    }
    if (config.pd.has_value() && config.pd->transfer.enabled) {
        if (config.pd->transfer.bandwidth_bytes_per_s <= 0.0) {
            serving_config_error(
                "Serving PD transfer bandwidth_bytes_per_s must be positive when enabled in " +
                source_name);
        }
        if (config.pd->transfer.efficiency <= 0.0 ||
            config.pd->transfer.efficiency > 1.0) {
            serving_config_error(
                "Serving PD transfer efficiency must be in (0, 1] in " +
                source_name);
        }
        const bool has_transfer_size_hint =
            (config.model.has_value() &&
             !config.pd->transfer.override_bytes_per_prompt_token) ||
            config.pd->transfer.bytes_per_prompt_token > 0;
        if (!has_transfer_size_hint) {
            serving_config_error(
                "PD transfer requires either a model shape or pd.transfer.bytes_per_prompt_token in " +
                source_name);
        }
    }
    if (config.topology.has_value()) {
        validate_layout(config.topology->colocated_layout, "colocated_layout");
        if (config.topology->prefill_layout.has_value()) {
            validate_layout(*config.topology->prefill_layout, "prefill_layout");
        }
        if (config.topology->decode_layout.has_value()) {
            validate_layout(*config.topology->decode_layout, "decode_layout");
        }
        if (config.runtime.architecture == ServingArchitecture::PdDisaggregated &&
            !has_disaggregated_layouts(*config.topology)) {
            serving_config_error(
                "PD serving requires topology.prefill_layout/decode_layout or PD deployment in " +
                source_name);
        }
    }
}

ServingConfig parse_serving_config(const json& root,
                                   const std::string& source_name) {
    const bool has_requests = root.contains("requests");
    const bool has_trace = root.contains("trace");
    if (has_requests == has_trace) {
        serving_config_error(
            "Serving config must contain exactly one of 'requests' or 'trace' in " +
            source_name);
    }

    ServingConfig config;
    config.runtime = parse_runtime(root, source_name);
    config.scheduler = parse_scheduler(root, source_name);
    config.slo = parse_slo(root, source_name);
    config.model = parse_model(root, source_name);
    config.pd = parse_pd(root, source_name);
    config.outputs = parse_outputs(root, source_name);
    config.cost_model = parse_cost_model(root, source_name,
                                         &config.prefill_collective,
                                         &config.decode_collective);
    config.topology = parse_topology(root, source_name);

    if (config.pd.has_value()) {
        if (config.pd->prefill_max_batch_tokens == 0) {
            config.pd->prefill_max_batch_tokens =
                config.scheduler.max_prefill_batch_tokens;
        }
        if (config.pd->decode_max_batch_requests == 0) {
            config.pd->decode_max_batch_requests =
                config.scheduler.max_decode_batch_requests;
        }
    }

    if (!config.topology.has_value()) {
        ServingTopologySpec topology;
        topology.colocated_layout = ParallelismLayoutSpec{
            "colocated", 1, 1, 1, 1, 1};
        if (config.runtime.architecture == ServingArchitecture::PdDisaggregated ||
            config.pd.has_value()) {
            topology.deployment =
                ServingTopologyDeployment::PrefillDecodeDisaggregated;
            const auto prefill_tp =
                config.pd.has_value() ? config.pd->prefill_tp_degree : 1;
            const auto decode_tp =
                config.pd.has_value() ? config.pd->decode_tp_degree : 1;
            topology.prefill_layout = ParallelismLayoutSpec{
                "prefill", prefill_tp, 1, 1, 1, 1};
            topology.decode_layout = ParallelismLayoutSpec{
                "decode", decode_tp, 1, 1, 1, 1};
        }
        config.topology = topology;
    }

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

    validate_config(config, source_name);
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
