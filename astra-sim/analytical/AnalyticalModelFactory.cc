#include "astra-sim/analytical/AnalyticalModelFactory.hh"

#include <stdexcept>

#include "astra-sim/analytical/AttentionFfnDisaggregationModel.hh"
#include "astra-sim/analytical/ServingDisaggColocatedModel.hh"
#include "astra-sim/analytical/ServingScaleModel.hh"
#include "astra-sim/analytical/TpPpCrossoverModel.hh"

namespace AstraSim {

std::unique_ptr<AnalyticalModel> AnalyticalModelFactory::create(
    const AnalyticalConfig& analytical_config,
    const AnalyticalContext& context) {
    switch (analytical_config.mode) {
    case AnalyticalMode::tp_pp_crossover:
        if (!analytical_config.tp_pp_crossover.has_value()) {
            throw std::runtime_error("Missing TP/PP crossover config");
        }
        return std::make_unique<TpPpCrossoverModel>(
            *analytical_config.tp_pp_crossover);
    case AnalyticalMode::serving_disagg_colocated:
        if (!analytical_config.serving_disagg_colocated.has_value()) {
            throw std::runtime_error(
                "Missing serving disaggregated-versus-colocated config");
        }
        return std::make_unique<ServingDisaggColocatedModel>(
            *analytical_config.serving_disagg_colocated, context);
    case AnalyticalMode::attention_ffn_disaggregation:
        if (!analytical_config.attention_ffn_disaggregation.has_value()) {
            throw std::runtime_error(
                "Missing attention/FFN disaggregation config");
        }
        return std::make_unique<AttentionFfnDisaggregationModel>(
            *analytical_config.attention_ffn_disaggregation);
    case AnalyticalMode::serving_scale:
        if (!analytical_config.serving_scale.has_value()) {
            throw std::runtime_error("Missing serving scale config");
        }
        return std::make_unique<ServingScaleModel>(
            *analytical_config.serving_scale, context);
    }

    throw std::runtime_error("Unsupported analytical mode");
}

}  // namespace AstraSim
