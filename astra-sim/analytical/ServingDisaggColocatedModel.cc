#include "astra-sim/analytical/ServingDisaggColocatedModel.hh"

#include <stdexcept>

#include "astra-sim/workload/ServingCoordinator.hh"

namespace AstraSim {

ServingDisaggColocatedModel::ServingDisaggColocatedModel(
    ServingDisaggColocatedConfig config,
    AnalyticalContext context)
    : config(std::move(config)),
      context(std::move(context)),
      coordinator(nullptr) {}

void ServingDisaggColocatedModel::run() {
    if (context.systems.empty()) {
        throw std::runtime_error(
            "Serving analytical mode requires instantiated ASTRA system objects");
    }

    coordinator = std::make_unique<ServingCoordinator>(
        context.systems, config.request_configuration,
        config.request_metrics_output, config.request_summary_output,
        config.request_run_metadata_output, context.binary_name,
        config.compute_scale, config.comm_scale);
    coordinator->fire();
}

}  // namespace AstraSim
