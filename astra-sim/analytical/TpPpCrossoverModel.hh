#ifndef ASTRASIM_ANALYTICAL_TP_PP_CROSSOVER_MODEL_HH
#define ASTRASIM_ANALYTICAL_TP_PP_CROSSOVER_MODEL_HH

#include "astra-sim/analytical/AnalyticalConfig.hh"
#include "astra-sim/analytical/AnalyticalModel.hh"

namespace AstraSim {

class TpPpCrossoverModel : public AnalyticalModel {
  public:
    explicit TpPpCrossoverModel(TpPpCrossoverConfig config);

    void run() override;

  private:
    TpPpCrossoverConfig config;
};

}  // namespace AstraSim

#endif  // ASTRASIM_ANALYTICAL_TP_PP_CROSSOVER_MODEL_HH
