#ifndef ASTRASIM_ANALYTICAL_ANALYTICAL_MODEL_FACTORY_HH
#define ASTRASIM_ANALYTICAL_ANALYTICAL_MODEL_FACTORY_HH

#include <memory>

#include "astra-sim/analytical/AnalyticalConfig.hh"
#include "astra-sim/analytical/AnalyticalModel.hh"

namespace AstraSim {

class AnalyticalModelFactory {
  public:
    static std::unique_ptr<AnalyticalModel> create(
        const AnalyticalConfig& analytical_config,
        const AnalyticalContext& context);
};

}  // namespace AstraSim

#endif  // ASTRASIM_ANALYTICAL_ANALYTICAL_MODEL_FACTORY_HH
