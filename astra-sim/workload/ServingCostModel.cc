#include "astra-sim/workload/ServingCostModel.hh"

#include <cmath>
#include <numeric>

#include "astra-sim/analytical/AnalyticalCostModels.hh"
#include "astra-sim/workload/ServingRuntime.hh"
#include "astra-sim/workload/ServingUtils.hh"

namespace AstraSim {

namespace {

uint64_t sum_batch_tokens(const ServingBatch& batch) {
    uint64_t total_tokens = 0;
    for (const auto& item : batch.items) {
        total_tokens += item.tokens;
    }
    return total_tokens;
}

long double ceil_preserving_near_integer(long double value) {
    constexpr long double epsilon = 1e-12L;
    const auto nearest_integer = std::round(value);
    if (std::abs(value - nearest_integer) <= epsilon) {
        return nearest_integer;
    }
    return std::ceil(value);
}

Tick seconds_to_ns(double seconds) {
    return static_cast<Tick>(std::llround(seconds * 1.0e9));
}

Tick scale_tick(Tick value, double scale) {
    return static_cast<Tick>(std::llround(static_cast<long double>(value) *
                                          static_cast<long double>(scale)));
}

Tick interpolate_tick(Tick low_value,
                      Tick high_value,
                      double fraction) {
    const auto low = static_cast<long double>(low_value);
    const auto high = static_cast<long double>(high_value);
    return static_cast<Tick>(
        std::llround(low + (high - low) * static_cast<long double>(fraction)));
}

double interpolate_double(double low_value,
                          double high_value,
                          double fraction) {
    return low_value + (high_value - low_value) * fraction;
}

uint64_t bytes_per_activation(const ServingModelConfig& model) {
    if (model.bytes_per_activation > 0) {
        return model.bytes_per_activation;
    }
    return std::max<uint64_t>(1, model.bytes_per_kv_element);
}

uint64_t layers_per_stage(const ServingModelConfig& model,
                          const ParallelismLayoutSpec& layout) {
    return static_cast<uint64_t>(std::ceil(
        static_cast<long double>(model.num_layers) /
        static_cast<long double>(std::max<uint64_t>(1, layout.pp_degree))));
}

bool is_moe_model(const ServingModelConfig& model) {
    return model.kind == "moe" || model.experts_per_layer > 0;
}

long double attention_flops_per_token(const ServingModelConfig& model,
                                      uint64_t sequence_length) {
    const auto hidden = static_cast<long double>(model.hidden_size);
    const auto seq = static_cast<long double>(std::max<uint64_t>(
        1, sequence_length));
    return 8.0L * hidden * hidden + 4.0L * seq * hidden;
}

long double dense_ffn_flops_per_token(const ServingModelConfig& model) {
    const auto hidden = static_cast<long double>(model.hidden_size);
    const auto ffn_hidden = static_cast<long double>(
        model.ffn_hidden_size > 0 ? model.ffn_hidden_size : model.hidden_size * 4);
    return 6.0L * hidden * ffn_hidden;
}

long double moe_ffn_flops_per_token(const ServingModelConfig& model) {
    const auto hidden = static_cast<long double>(model.hidden_size);
    const auto experts = static_cast<long double>(std::max<uint64_t>(
        1, model.experts_per_layer));
    const auto active_experts = static_cast<long double>(std::max<uint64_t>(
        1, model.active_experts_per_token));
    const auto expert_hidden = static_cast<long double>(
        model.expert_hidden_size > 0 ? model.expert_hidden_size
                                     : model.hidden_size * 4);
    const auto router = 2.0L * hidden * experts;
    const auto expert = 6.0L * active_experts * hidden * expert_hidden;
    return router + expert;
}

double default_interconnect_efficiency(const InterconnectSpec* interconnect) {
    return interconnect != nullptr ? interconnect->efficiency : 1.0;
}

}  // namespace

ServingCostModel::ServingCostModel(const ServingConfig& config,
                                   double compute_scale,
                                   double comm_scale)
    : config(config),
      compute_scale(compute_scale),
      comm_scale(comm_scale) {}

Tick ServingCostModel::estimate_decode_step_latency_ns() const {
    const auto& curve = config.cost_model.decode_step_latency_curve;
    if (!curve.enabled || !config.target_request_rate_per_second.has_value() ||
        curve.points.empty()) {
        return scale_tick(config.cost_model.decode_step_latency_ns,
                          compute_scale);
    }

    const auto target_rate = *config.target_request_rate_per_second;
    const auto& points = curve.points;
    if (target_rate <= points.front().request_rate_per_second) {
        return scale_tick(points.front().decode_step_latency_ns, compute_scale);
    }
    if (target_rate >= points.back().request_rate_per_second) {
        return scale_tick(points.back().decode_step_latency_ns, compute_scale);
    }

    for (size_t index = 1; index < points.size(); ++index) {
        const auto& lower = points[index - 1];
        const auto& upper = points[index];
        if (target_rate <= upper.request_rate_per_second) {
            const auto fraction =
                (target_rate - lower.request_rate_per_second) /
                (upper.request_rate_per_second -
                 lower.request_rate_per_second);
            return scale_tick(
                interpolate_tick(lower.decode_step_latency_ns,
                                 upper.decode_step_latency_ns, fraction),
                compute_scale);
        }
    }

    return scale_tick(config.cost_model.decode_step_latency_ns, compute_scale);
}

ServingFirstTokenBackpressurePoint
ServingCostModel::estimate_first_token_backpressure_point() const {
    const auto& curve = config.cost_model.first_token_backpressure_curve;
    if (!curve.enabled || !config.target_request_rate_per_second.has_value() ||
        curve.points.empty()) {
        return ServingFirstTokenBackpressurePoint{
            0.0,
            config.cost_model.first_token_latency_ns,
            0.0,
            0.0,
        };
    }

    const auto target_rate = *config.target_request_rate_per_second;
    const auto& points = curve.points;
    if (target_rate <= points.front().request_rate_per_second) {
        return points.front();
    }
    if (target_rate >= points.back().request_rate_per_second) {
        return points.back();
    }

    for (size_t index = 1; index < points.size(); ++index) {
        const auto& lower = points[index - 1];
        const auto& upper = points[index];
        if (target_rate <= upper.request_rate_per_second) {
            const auto fraction =
                (target_rate - lower.request_rate_per_second) /
                (upper.request_rate_per_second -
                 lower.request_rate_per_second);
            return ServingFirstTokenBackpressurePoint{
                target_rate,
                interpolate_tick(lower.base_latency_ns, upper.base_latency_ns,
                                 fraction),
                interpolate_double(lower.knee_request_index,
                                   upper.knee_request_index, fraction),
                interpolate_double(
                    lower.latency_ns_per_request_after_knee,
                    upper.latency_ns_per_request_after_knee, fraction),
            };
        }
    }

    return ServingFirstTokenBackpressurePoint{
        0.0,
        config.cost_model.first_token_latency_ns,
        0.0,
        0.0,
    };
}

Tick ServingCostModel::estimate_first_token_latency_ns(
    const ServingRequestState& request) const {
    const auto point = estimate_first_token_backpressure_point();
    const auto request_rank =
        static_cast<double>(request.spec.original_index);
    const auto extra = std::max(0.0, request_rank - point.knee_request_index) *
                       point.latency_ns_per_request_after_knee;
    return point.base_latency_ns +
           static_cast<Tick>(std::llround(static_cast<long double>(extra)));
}

ServingStageBreakdown ServingCostModel::estimate_serial_prefill_breakdown_ns(
    uint64_t prompt_tokens) const {
    ServingStageBreakdown breakdown;
    breakdown.base_latency_ns =
        scale_tick(config.cost_model.prefill.base_latency_ns, compute_scale);
    breakdown.attention_compute_ns = scale_tick(
        prompt_tokens *
            config.cost_model.prefill.attention_compute_ns_per_token,
        compute_scale);
    breakdown.ffn_or_expert_compute_ns = scale_tick(
        prompt_tokens *
            config.cost_model.prefill.ffn_or_expert_compute_ns_per_token,
        compute_scale);
    breakdown.tp_collective_ns = scale_tick(
        prompt_tokens * config.cost_model.prefill.tp_collective_ns_per_token,
        comm_scale);
    breakdown.pp_activation_ns = scale_tick(
        prompt_tokens * config.cost_model.prefill.pp_activation_ns_per_token,
        comm_scale);
    breakdown.ep_dispatch_ns = scale_tick(
        prompt_tokens * config.cost_model.prefill.ep_dispatch_ns_per_token,
        comm_scale);
    breakdown.dp_attention_sync_ns = scale_tick(
        prompt_tokens *
            config.cost_model.prefill.dp_attention_sync_ns_per_token,
        comm_scale);
    if (breakdown.total_ns() == breakdown.base_latency_ns) {
        breakdown.attention_compute_ns = scale_tick(
            prompt_tokens * config.cost_model.prefill_compute_ns_per_token,
            compute_scale);
    }
    return breakdown;
}

Tick ServingCostModel::estimate_serial_prefill_ns(uint64_t prompt_tokens) const {
    return estimate_serial_prefill_breakdown_ns(prompt_tokens).total_ns();
}

ServingStageBreakdown ServingCostModel::estimate_serial_decode_breakdown_ns(
    bool include_base_latency,
    uint64_t token_count) const {
    ServingStageBreakdown breakdown;
    breakdown.base_latency_ns = scale_tick(
        include_base_latency ? config.cost_model.decode.base_latency_ns : 0,
        compute_scale);
    breakdown.step_latency_ns = estimate_decode_step_latency_ns();
    breakdown.attention_compute_ns = scale_tick(
        token_count * config.cost_model.decode.attention_compute_ns_per_token,
        compute_scale);
    breakdown.ffn_or_expert_compute_ns = scale_tick(
        token_count *
            config.cost_model.decode.ffn_or_expert_compute_ns_per_token,
        compute_scale);
    breakdown.tp_collective_ns = scale_tick(
        token_count * config.cost_model.decode.tp_collective_ns_per_token,
        comm_scale);
    breakdown.pp_activation_ns = scale_tick(
        token_count * config.cost_model.decode.pp_activation_ns_per_token,
        comm_scale);
    breakdown.ep_dispatch_ns = scale_tick(
        token_count * config.cost_model.decode.ep_dispatch_ns_per_token,
        comm_scale);
    breakdown.dp_attention_sync_ns = scale_tick(
        token_count * config.cost_model.decode.dp_attention_sync_ns_per_token,
        comm_scale);
    if (breakdown.total_ns() == breakdown.base_latency_ns) {
        breakdown.attention_compute_ns = scale_tick(
            token_count * config.cost_model.decode_compute_ns_per_token,
            compute_scale);
    }
    breakdown.attention_compute_ns = scale_tick(
        breakdown.attention_compute_ns,
        config.cost_model.decode.interference_factor);
    breakdown.ffn_or_expert_compute_ns = scale_tick(
        breakdown.ffn_or_expert_compute_ns,
        config.cost_model.decode.interference_factor);
    breakdown.tp_collective_ns = scale_tick(
        breakdown.tp_collective_ns,
        config.cost_model.decode.interference_factor);
    breakdown.pp_activation_ns = scale_tick(
        breakdown.pp_activation_ns,
        config.cost_model.decode.interference_factor);
    breakdown.ep_dispatch_ns = scale_tick(
        breakdown.ep_dispatch_ns,
        config.cost_model.decode.interference_factor);
    breakdown.dp_attention_sync_ns = scale_tick(
        breakdown.dp_attention_sync_ns,
        config.cost_model.decode.interference_factor);
    return breakdown;
}

Tick ServingCostModel::estimate_serial_decode_ns(bool include_base_latency,
                                                 uint64_t token_count) const {
    return estimate_serial_decode_breakdown_ns(include_base_latency, token_count)
        .total_ns();
}

ServingStageBreakdown ServingCostModel::estimate_stage_breakdown(
    const ServingBatch& batch,
    const ServingCostModelConfig::StageComponentConfig& stage_config,
    bool is_decode_stage) const {
    ServingStageBreakdown breakdown;
    const auto total_tokens = sum_batch_tokens(batch);
    const auto stage_multiplier =
        stage_config.batch_efficiency *
        (is_decode_stage ? stage_config.interference_factor : 1.0);

    breakdown.base_latency_ns = scale_tick(stage_config.base_latency_ns,
                                           compute_scale);

    const bool can_use_topology_formula =
        config.model.has_value() && config.cluster.has_value() &&
        !config.interconnects.empty();

    if (!can_use_topology_formula) {
        breakdown.attention_compute_ns = scale_tick(
            scale_tick(total_tokens *
                           stage_config.attention_compute_ns_per_token,
                       compute_scale),
            stage_multiplier);
        breakdown.ffn_or_expert_compute_ns = scale_tick(
            scale_tick(total_tokens *
                           stage_config.ffn_or_expert_compute_ns_per_token,
                       compute_scale),
            stage_multiplier);
        breakdown.tp_collective_ns = scale_tick(
            scale_tick(total_tokens * stage_config.tp_collective_ns_per_token,
                       comm_scale),
            stage_multiplier);
        breakdown.pp_activation_ns = scale_tick(
            scale_tick(total_tokens * stage_config.pp_activation_ns_per_token,
                       comm_scale),
            stage_multiplier);
        breakdown.ep_dispatch_ns = scale_tick(
            scale_tick(total_tokens * stage_config.ep_dispatch_ns_per_token,
                       comm_scale),
            stage_multiplier);
        breakdown.dp_attention_sync_ns = scale_tick(
            scale_tick(total_tokens *
                           stage_config.dp_attention_sync_ns_per_token,
                       comm_scale),
            stage_multiplier);
        if (breakdown.total_ns() == breakdown.base_latency_ns) {
            breakdown.attention_compute_ns = scale_tick(
                scale_tick(total_tokens *
                               (is_decode_stage
                                    ? config.cost_model.decode_compute_ns_per_token
                                    : config.cost_model.prefill_compute_ns_per_token),
                           compute_scale),
                stage_multiplier);
        }
        return breakdown;
    }

    const auto& model = *config.model;
    const auto* gpu = config.cluster->find_device(DeviceType::GPU);
    const auto* interconnect =
        find_interconnect(config.interconnects, DeviceType::GPU, DeviceType::GPU);
    if (gpu == nullptr || interconnect == nullptr) {
        breakdown.attention_compute_ns = scale_tick(
            scale_tick(total_tokens *
                           (is_decode_stage
                                ? config.cost_model.decode_compute_ns_per_token
                                : config.cost_model.prefill_compute_ns_per_token),
                       compute_scale),
            stage_multiplier);
        return breakdown;
    }

    const auto stage_layers = layers_per_stage(model, batch.layout);
    const auto seq_hint = std::max<uint64_t>(1, batch.sequence_length_hint);
    const auto activation_bytes = bytes_per_activation(model);
    const auto layout_tp = std::max<uint64_t>(1, batch.layout.tp_degree);
    const auto layout_pp = std::max<uint64_t>(1, batch.layout.pp_degree);
    const auto layout_ep = std::max<uint64_t>(1, batch.layout.ep_degree);
    const auto layout_dpattn =
        std::max<uint64_t>(1, batch.layout.dp_attention_degree);

    const auto attention_flops =
        static_cast<long double>(total_tokens) *
        static_cast<long double>(stage_layers) *
        attention_flops_per_token(model, seq_hint);
    const auto ffn_flops_per_token = is_moe_model(model)
                                         ? moe_ffn_flops_per_token(model)
                                         : dense_ffn_flops_per_token(model);
    const auto ffn_flops =
        static_cast<long double>(total_tokens) *
        static_cast<long double>(stage_layers) * ffn_flops_per_token;

    const auto attention_formula_ns = seconds_to_ns(compute_time_seconds(
        attention_flops, gpu->peak_flops, 1.0, static_cast<uint32_t>(layout_tp)));
    const auto ffn_parallel_units =
        is_moe_model(model) ? layout_tp * layout_ep : layout_tp;
    const auto ffn_formula_ns = seconds_to_ns(compute_time_seconds(
        ffn_flops, gpu->peak_flops, 1.0,
        static_cast<uint32_t>(std::max<uint64_t>(1, ffn_parallel_units))));

    const auto collective_bytes =
        total_tokens * model.hidden_size * activation_bytes;
    const auto tp_formula_ns =
        layout_tp > 1
            ? seconds_to_ns(static_cast<double>(stage_layers) * 2.0 *
                            ring_all_reduce_time_seconds(
                                collective_bytes,
                                static_cast<uint32_t>(layout_tp),
                                interconnect->bandwidth_bytes_per_s,
                                interconnect->latency_ns,
                                default_interconnect_efficiency(interconnect)))
            : 0;

    const auto pp_transfer_ns =
        layout_pp > 1
            ? seconds_to_ns(point_to_point_time_seconds(
                  collective_bytes, interconnect->bandwidth_bytes_per_s,
                  interconnect->latency_ns,
                  default_interconnect_efficiency(interconnect)))
            : 0;
    const auto pp_bubble_ns =
        layout_pp > 1
            ? static_cast<Tick>(std::llround(
                  static_cast<long double>(layout_pp - 1) *
                  static_cast<long double>(attention_formula_ns + ffn_formula_ns +
                                           tp_formula_ns) /
                  static_cast<long double>(std::max<uint64_t>(1, total_tokens))))
            : 0;
    const auto pp_formula_ns = pp_transfer_ns + pp_bubble_ns;

    const auto ep_bytes =
        total_tokens * model.hidden_size * activation_bytes * stage_layers * 2;
    const auto ep_formula_ns =
        (layout_ep > 1 && is_moe_model(model))
            ? seconds_to_ns(all_to_all_time_seconds(
                  ep_bytes, static_cast<uint32_t>(layout_ep),
                  interconnect->bandwidth_bytes_per_s,
                  interconnect->latency_ns,
                  default_interconnect_efficiency(interconnect)))
            : 0;

    const auto dp_sync_bytes =
        total_tokens * model.kv_heads * model.head_dim * 2 *
        model.bytes_per_kv_element * stage_layers;
    const auto dp_formula_ns =
        layout_dpattn > 1
            ? seconds_to_ns(ring_all_gather_time_seconds(
                  dp_sync_bytes / layout_dpattn,
                  static_cast<uint32_t>(layout_dpattn),
                  interconnect->bandwidth_bytes_per_s,
                  interconnect->latency_ns,
                  default_interconnect_efficiency(interconnect)))
            : 0;

    const auto attention_ns =
        stage_config.attention_compute_ns_per_token > 0
            ? total_tokens * stage_config.attention_compute_ns_per_token
            : attention_formula_ns;
    const auto ffn_ns =
        stage_config.ffn_or_expert_compute_ns_per_token > 0
            ? total_tokens * stage_config.ffn_or_expert_compute_ns_per_token
            : ffn_formula_ns;
    const auto tp_ns = stage_config.tp_collective_ns_per_token > 0
                           ? total_tokens * stage_config.tp_collective_ns_per_token
                           : tp_formula_ns;
    const auto pp_ns = stage_config.pp_activation_ns_per_token > 0
                           ? total_tokens * stage_config.pp_activation_ns_per_token
                           : pp_formula_ns;
    const auto ep_ns = stage_config.ep_dispatch_ns_per_token > 0
                           ? total_tokens * stage_config.ep_dispatch_ns_per_token
                           : ep_formula_ns;
    const auto dp_ns =
        stage_config.dp_attention_sync_ns_per_token > 0
            ? total_tokens * stage_config.dp_attention_sync_ns_per_token
            : dp_formula_ns;

    breakdown.attention_compute_ns =
        scale_tick(scale_tick(attention_ns, compute_scale), stage_multiplier);
    breakdown.ffn_or_expert_compute_ns =
        scale_tick(scale_tick(ffn_ns, compute_scale), stage_multiplier);
    breakdown.tp_collective_ns =
        scale_tick(scale_tick(tp_ns, comm_scale), stage_multiplier);
    breakdown.pp_activation_ns =
        scale_tick(scale_tick(pp_ns, comm_scale), stage_multiplier);
    breakdown.ep_dispatch_ns =
        scale_tick(scale_tick(ep_ns, comm_scale), stage_multiplier);
    breakdown.dp_attention_sync_ns =
        scale_tick(scale_tick(dp_ns, comm_scale), stage_multiplier);
    return breakdown;
}

ServingStageBreakdown ServingCostModel::estimate_prefill_breakdown(
    const ServingBatch& batch) const {
    return estimate_stage_breakdown(batch, config.cost_model.prefill, false);
}

Tick ServingCostModel::estimate_prefill_ns(const ServingBatch& batch) const {
    return estimate_prefill_breakdown(batch).total_ns();
}

ServingStageBreakdown ServingCostModel::estimate_decode_breakdown(
    const ServingBatch& batch) const {
    ServingBatch adjusted_batch = batch;
    if (!adjusted_batch.include_base_latency) {
        adjusted_batch.layout = batch.layout;
    }
    auto breakdown =
        estimate_stage_breakdown(adjusted_batch, config.cost_model.decode, true);
    if (!batch.include_base_latency) {
        breakdown.base_latency_ns = 0;
    }
    breakdown.step_latency_ns = estimate_decode_step_latency_ns();
    return breakdown;
}

Tick ServingCostModel::estimate_decode_ns(const ServingBatch& batch) const {
    return estimate_decode_breakdown(batch).total_ns();
}

uint64_t ServingCostModel::estimate_kv_transfer_bytes(
    const ServingRequestState& request) const {
    if (config.pd.has_value()) {
        const auto& transfer = config.pd->transfer;
        if (transfer.override_bytes_per_prompt_token &&
            transfer.bytes_per_prompt_token > 0) {
            return request.spec.prompt_tokens * transfer.bytes_per_prompt_token;
        }
    }

    if (config.model.has_value()) {
        const auto& model = *config.model;
        const auto kv_bytes_per_prompt_token =
            model.num_layers * model.kv_heads * model.head_dim * 2 *
            model.bytes_per_kv_element;
        return request.spec.prompt_tokens * kv_bytes_per_prompt_token;
    }

    if (config.pd.has_value()) {
        return request.spec.prompt_tokens *
               config.pd->transfer.bytes_per_prompt_token;
    }
    return 0;
}

ServingStageBreakdown ServingCostModel::estimate_transfer_breakdown(
    const ServingRequestState& request) const {
    ServingStageBreakdown breakdown;
    if (!config.pd.has_value() || !config.pd->transfer.enabled) {
        return breakdown;
    }
    const auto& transfer = config.pd->transfer;
    const auto transfer_bytes = estimate_kv_transfer_bytes(request);
    if (transfer_bytes == 0) {
        return breakdown;
    }

    const auto effective_bandwidth =
        transfer.bandwidth_bytes_per_s * transfer.efficiency;
    const auto bandwidth_component_ns = ceil_preserving_near_integer(
        static_cast<long double>(transfer_bytes) /
        static_cast<long double>(effective_bandwidth) * 1000000000.0L);
    breakdown.base_latency_ns = transfer.latency_ns;
    breakdown.pd_kv_transfer_ns =
        static_cast<Tick>(std::llround(bandwidth_component_ns));
    return breakdown;
}

Tick ServingCostModel::estimate_transfer_ns(
    const ServingRequestState& request) const {
    return estimate_transfer_breakdown(request).total_ns();
}

uint64_t ServingCostModel::scale_collective_size(uint64_t bytes) const {
    if (bytes == 0) {
        return 0;
    }
    const auto scaled_size =
        std::llround(static_cast<long double>(bytes) * comm_scale);
    return scaled_size > 0 ? static_cast<uint64_t>(scaled_size) : 0;
}

Tick ServingCostModel::estimate_collective_ns(
    const ServingCollectiveSpec& collective) const {
    if (!collective.enabled || collective.size_bytes == 0) {
        return 0;
    }
    return scale_collective_size(collective.size_bytes);
}

}  // namespace AstraSim
