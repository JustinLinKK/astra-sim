#ifndef ASTRASIM_WORKLOAD_SERVING_TOPOLOGY_SERVING_BATCH_BUILDER_HH
#define ASTRASIM_WORKLOAD_SERVING_TOPOLOGY_SERVING_BATCH_BUILDER_HH

#include <deque>
#include <optional>
#include <vector>

#include "astra-sim/workload/ServingTypes.hh"

namespace AstraSim {

struct ServingRequestState;
struct ServingSchedulerConfig;
struct ServingPdConfig;

class ServingBatchBuilder {
  public:
    static std::optional<ServingBatch> build_prefill_from_running(
        uint64_t batch_id,
        ServingStageType stage,
        size_t group_id,
        size_t replica_id,
        const ParallelismLayoutSpec& layout,
        const std::vector<size_t>& running_requests,
        const std::vector<ServingRequestState>& requests,
        const ServingSchedulerConfig& scheduler,
        bool chunking_enabled);

    static std::optional<ServingBatch> build_decode_from_running(
        uint64_t batch_id,
        ServingStageType stage,
        size_t group_id,
        size_t replica_id,
        const ParallelismLayoutSpec& layout,
        const std::vector<size_t>& running_requests,
        const std::vector<ServingRequestState>& requests,
        uint64_t request_limit);

    static std::optional<ServingBatch> build_prefill_from_queue(
        uint64_t batch_id,
        ServingStageType stage,
        size_t group_id,
        size_t replica_id,
        const ParallelismLayoutSpec& layout,
        std::deque<size_t>& queue,
        const std::vector<ServingRequestState>& requests,
        const ServingPdConfig& pd,
        const ServingSchedulerConfig& scheduler,
        bool chunking_enabled);

    static std::optional<ServingBatch> build_decode_from_queue(
        uint64_t batch_id,
        ServingStageType stage,
        size_t group_id,
        size_t replica_id,
        const ParallelismLayoutSpec& layout,
        std::deque<size_t>& queue,
        const std::vector<ServingRequestState>& requests,
        uint64_t request_limit);
};

}  // namespace AstraSim

#endif  // ASTRASIM_WORKLOAD_SERVING_TOPOLOGY_SERVING_BATCH_BUILDER_HH
