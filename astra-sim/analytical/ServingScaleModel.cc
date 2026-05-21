#include "astra-sim/analytical/ServingScaleModel.hh"

#include <stdexcept>

namespace AstraSim {

namespace {

ServingModelConfig make_dense_serving_model(const DenseModelSpec& dense) {
    ServingModelConfig model;
    model.name = dense.name;
    model.kind = "dense";
    model.num_layers = dense.num_layers;
    model.hidden_size = dense.hidden_size;
    model.ffn_hidden_size = dense.ffn_hidden_size;
    model.attention_heads = dense.attention_heads;
    model.kv_heads = dense.kv_heads;
    model.head_dim = dense.attention_heads > 0
                         ? dense.hidden_size / dense.attention_heads
                         : 0;
    model.bytes_per_parameter = dense.bytes_per_parameter;
    model.bytes_per_activation = dense.bytes_per_activation;
    model.bytes_per_kv_element = dense.bytes_per_kv_element;
    return model;
}

ServingModelConfig make_moe_serving_model(const MoEModelSpec& moe) {
    ServingModelConfig model;
    model.name = moe.name;
    model.kind = "moe";
    model.num_layers = moe.num_layers;
    model.hidden_size = moe.hidden_size;
    model.attention_heads = moe.attention_heads;
    model.kv_heads = moe.kv_heads;
    model.head_dim = moe.attention_heads > 0
                         ? moe.hidden_size / moe.attention_heads
                         : 0;
    model.experts_per_layer = moe.experts_per_layer;
    model.active_experts_per_token = moe.active_experts_per_token;
    model.expert_hidden_size = moe.expert_hidden_size;
    model.bytes_per_parameter = moe.bytes_per_parameter;
    model.bytes_per_activation = moe.bytes_per_activation;
    model.bytes_per_kv_element = moe.bytes_per_kv_element;
    return model;
}

}  // namespace

ServingScaleModel::ServingScaleModel(ServingScaleConfig config,
                                     AnalyticalContext context)
    : config(std::move(config)),
      context(std::move(context)),
      coordinator(nullptr) {}

void ServingScaleModel::run() {
    if (context.systems.empty()) {
        throw std::runtime_error(
            "Serving scale mode requires instantiated ASTRA system objects");
    }

    auto serving_config = ServingConfig::load_from_file(config.request_configuration);
    if (config.dense_model.has_value()) {
        serving_config.model = make_dense_serving_model(*config.dense_model);
    } else if (config.moe_model.has_value()) {
        serving_config.model = make_moe_serving_model(*config.moe_model);
    }
    serving_config.topology = config.topology;
    serving_config.cluster = config.cluster;
    serving_config.interconnects = config.interconnects;
    serving_config.cost_model.topology_aware_enabled = true;

    const ServingOutputPaths outputs{
        config.request_configuration,
        config.request_metrics_output,
        config.request_summary_output,
        config.request_run_metadata_output,
        serving_config.outputs.event_trace_output,
        serving_config.outputs.stage_metrics_output,
    };

    coordinator = std::make_unique<ServingCoordinator>(
        context.systems, std::move(serving_config), outputs, context.binary_name,
        config.compute_scale, config.comm_scale);
    coordinator->fire();
}

}  // namespace AstraSim
