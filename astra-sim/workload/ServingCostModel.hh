#ifndef __SERVING_COST_MODEL_HH__
#define __SERVING_COST_MODEL_HH__

#include <cstdint>

#include "astra-sim/workload/ServingConfig.hh"
#include "astra-sim/workload/ServingTypes.hh"

namespace AstraSim {

struct ServingRequestState;

class ServingCostModel {
  public:
    ServingCostModel(const ServingConfig& config,
                     double compute_scale,
                     double comm_scale);

    ServingStageBreakdown estimate_serial_prefill_breakdown_ns(
        uint64_t prompt_tokens) const;
    Tick estimate_serial_prefill_ns(uint64_t prompt_tokens) const;
    ServingStageBreakdown estimate_serial_decode_breakdown_ns(
        bool include_base_latency,
        uint64_t token_count) const;
    Tick estimate_serial_decode_ns(bool include_base_latency,
                                   uint64_t token_count) const;
    ServingStageBreakdown estimate_prefill_breakdown(
        const ServingBatch& batch) const;
    Tick estimate_prefill_ns(const ServingBatch& batch) const;
    ServingStageBreakdown estimate_decode_breakdown(
        const ServingBatch& batch) const;
    Tick estimate_decode_ns(const ServingBatch& batch) const;
    ServingStageBreakdown estimate_transfer_breakdown(
        const ServingRequestState& request) const;
    Tick estimate_transfer_ns(const ServingRequestState& request) const;
    uint64_t estimate_kv_transfer_bytes(const ServingRequestState& request) const;
    uint64_t scale_collective_size(uint64_t bytes) const;
    Tick estimate_collective_ns(const ServingCollectiveSpec& collective) const;

  private:
    ServingStageBreakdown estimate_stage_breakdown(
        const ServingBatch& batch,
        const ServingCostModelConfig::StageComponentConfig& stage_config,
        bool is_decode_stage) const;

    const ServingConfig& config;
    double compute_scale;
    double comm_scale;
};

}  // namespace AstraSim

#endif /* __SERVING_COST_MODEL_HH__ */
