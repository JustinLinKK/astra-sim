#ifndef ASTRASIM_ANALYTICAL_SERVING_SCALE_MODEL_HH
#define ASTRASIM_ANALYTICAL_SERVING_SCALE_MODEL_HH

#include <memory>

#include "astra-sim/analytical/AnalyticalConfig.hh"
#include "astra-sim/analytical/AnalyticalModel.hh"
#include "astra-sim/workload/ServingCoordinator.hh"

namespace AstraSim {

class ServingScaleModel : public AnalyticalModel {
  public:
    ServingScaleModel(ServingScaleConfig config, AnalyticalContext context);

    void run() override;

  private:
    ServingScaleConfig config;
    AnalyticalContext context;
    std::unique_ptr<ServingCoordinator> coordinator;
};

}  // namespace AstraSim

#endif  // ASTRASIM_ANALYTICAL_SERVING_SCALE_MODEL_HH
