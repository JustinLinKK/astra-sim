#ifndef ASTRASIM_ANALYTICAL_SERVING_DISAGG_COLOCATED_MODEL_HH
#define ASTRASIM_ANALYTICAL_SERVING_DISAGG_COLOCATED_MODEL_HH

#include <memory>

#include "astra-sim/analytical/AnalyticalConfig.hh"
#include "astra-sim/analytical/AnalyticalModel.hh"
#include "astra-sim/workload/ServingCoordinator.hh"

namespace AstraSim {

class ServingDisaggColocatedModel : public AnalyticalModel {
  public:
    ServingDisaggColocatedModel(ServingDisaggColocatedConfig config,
                                AnalyticalContext context);

    void run() override;

  private:
    ServingDisaggColocatedConfig config;
    AnalyticalContext context;
    std::unique_ptr<ServingCoordinator> coordinator;
};

}  // namespace AstraSim

#endif  // ASTRASIM_ANALYTICAL_SERVING_DISAGG_COLOCATED_MODEL_HH
