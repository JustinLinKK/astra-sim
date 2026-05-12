#ifndef ASTRASIM_WORKLOAD_SERVING_TOPOLOGY_SERVING_TOPOLOGY_HH
#define ASTRASIM_WORKLOAD_SERVING_TOPOLOGY_SERVING_TOPOLOGY_HH

#include <optional>
#include <vector>

#include "astra-sim/workload/ServingConfig.hh"
#include "astra-sim/workload/ServingTypes.hh"

namespace AstraSim {

class ServingTopology {
  public:
    ServingTopology() = default;
    ServingTopology(const ServingConfig& config, size_t system_count);

    size_t replica_count() const;
    const std::vector<ServingWorkerGroup>& groups() const;
    const ServingWorkerGroup& group(size_t group_id) const;
    bool has_expert_groups() const;

    size_t assign_replica(const ServingRequestSpec& request) const;

    std::optional<size_t> find_idle_group(ServingWorkerGroupRole role) const;
    std::optional<size_t> find_idle_group_for_replica(
        ServingWorkerGroupRole role,
        size_t replica_id) const;

    std::vector<size_t> group_ids_for_role(ServingWorkerGroupRole role) const;
    std::vector<size_t> group_ids_for_role(ServingWorkerGroupRole role,
                                           size_t replica_id) const;

    void mark_group_busy(size_t group_id, uint64_t batch_id);
    void mark_group_idle(size_t group_id);

    const ParallelismLayoutSpec& colocated_layout() const;
    const ParallelismLayoutSpec& prefill_layout() const;
    const ParallelismLayoutSpec& decode_layout() const;

  private:
    std::vector<ServingWorkerGroup> worker_groups;
    ServingTopologySpec topology_spec;
};

}  // namespace AstraSim

#endif  // ASTRASIM_WORKLOAD_SERVING_TOPOLOGY_SERVING_TOPOLOGY_HH
