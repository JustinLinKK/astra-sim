#ifndef ASTRASIM_ANALYTICAL_ATTENTION_FFN_DISAGGREGATION_MODEL_HH
#define ASTRASIM_ANALYTICAL_ATTENTION_FFN_DISAGGREGATION_MODEL_HH

#include "astra-sim/analytical/AnalyticalConfig.hh"
#include "astra-sim/analytical/AnalyticalModel.hh"

namespace AstraSim {

class AttentionFfnDisaggregationModel : public AnalyticalModel {
  public:
    explicit AttentionFfnDisaggregationModel(
        AttentionFfnDisaggregationConfig config);

    void run() override;

  private:
    AttentionFfnDisaggregationConfig config;
};

}  // namespace AstraSim

#endif  // ASTRASIM_ANALYTICAL_ATTENTION_FFN_DISAGGREGATION_MODEL_HH
