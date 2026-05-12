#include "astra-sim/workload/serving/topology/ServingTopology.hh"

#include <algorithm>

namespace AstraSim {

namespace {

size_t replica_count_from_layout(const ParallelismLayoutSpec& layout) {
    return std::max<uint64_t>(1, layout.dp_replica_count);
}

size_t groups_for_replica(uint64_t total_groups,
                          size_t replica_count,
                          size_t replica_id) {
    if (replica_count == 0) {
        return 1;
    }
    const auto base_groups = total_groups / replica_count;
    const auto remainder = total_groups % replica_count;
    const auto local_groups =
        base_groups + (replica_id < remainder ? 1ULL : 0ULL);
    return static_cast<size_t>(std::max<uint64_t>(1, local_groups));
}

ServingWorkerGroup make_group(size_t group_id,
                              size_t replica_id,
                              ServingWorkerGroupRole role,
                              const ParallelismLayoutSpec& layout,
                              const std::string& name,
                              size_t worker_count) {
    ServingWorkerGroup group;
    group.group_id = group_id;
    group.replica_id = replica_id;
    group.role = role;
    group.layout = layout;
    group.name = name;
    group.worker_count = std::max<size_t>(1, worker_count);
    return group;
}

}  // namespace

ServingTopology::ServingTopology(const ServingConfig& config, size_t system_count)
    : topology_spec(config.topology.value_or(ServingTopologySpec{})) {
    const auto replica_count = replica_count_from_layout(
        effective_decode_layout(topology_spec));
    size_t next_group_id = 0;

    if (config.runtime.architecture == ServingArchitecture::PdDisaggregated ||
        topology_spec.deployment ==
            ServingTopologyDeployment::PrefillDecodeDisaggregated) {
        const auto prefill_workers = config.pd.has_value()
                                         ? std::max<uint64_t>(
                                               1, config.pd->prefill_workers)
                                         : 1;
        const auto decode_workers = config.pd.has_value()
                                        ? std::max<uint64_t>(
                                              1, config.pd->decode_workers)
                                        : 1;
        for (size_t replica_id = 0; replica_id < replica_count; ++replica_id) {
            const auto prefill_group_count =
                groups_for_replica(prefill_workers, replica_count, replica_id);
            const auto decode_group_count =
                groups_for_replica(decode_workers, replica_count, replica_id);

            for (size_t local_index = 0; local_index < prefill_group_count;
                 ++local_index) {
                worker_groups.push_back(make_group(
                    next_group_id++, replica_id,
                    ServingWorkerGroupRole::Prefill, prefill_layout(),
                    "prefill-r" + std::to_string(replica_id) + "-g" +
                        std::to_string(local_index),
                    1));
            }
            for (size_t local_index = 0; local_index < decode_group_count;
                 ++local_index) {
                worker_groups.push_back(make_group(
                    next_group_id++, replica_id,
                    ServingWorkerGroupRole::Decode, decode_layout(),
                    "decode-r" + std::to_string(replica_id) + "-g" +
                        std::to_string(local_index),
                    1));
            }
            if (decode_layout().ep_degree > 1) {
                worker_groups.push_back(make_group(
                    next_group_id++, replica_id, ServingWorkerGroupRole::Expert,
                    decode_layout(), "expert-r" + std::to_string(replica_id),
                    static_cast<size_t>(decode_layout().ep_degree)));
            }
        }
        return;
    }

    const auto worker_budget = std::max<size_t>(1, system_count);
    for (size_t replica_id = 0; replica_id < replica_count; ++replica_id) {
        worker_groups.push_back(make_group(
            next_group_id++, replica_id, ServingWorkerGroupRole::Colocated,
            colocated_layout(), "colocated-r" + std::to_string(replica_id),
            std::max<size_t>(1, worker_budget / replica_count)));
        if (colocated_layout().ep_degree > 1) {
            worker_groups.push_back(make_group(
                next_group_id++, replica_id, ServingWorkerGroupRole::Expert,
                colocated_layout(), "expert-r" + std::to_string(replica_id),
                static_cast<size_t>(colocated_layout().ep_degree)));
        }
    }
}

size_t ServingTopology::replica_count() const {
    return replica_count_from_layout(effective_decode_layout(topology_spec));
}

const std::vector<ServingWorkerGroup>& ServingTopology::groups() const {
    return worker_groups;
}

const ServingWorkerGroup& ServingTopology::group(size_t group_id) const {
    return worker_groups.at(group_id);
}

bool ServingTopology::has_expert_groups() const {
    return std::any_of(
        worker_groups.begin(), worker_groups.end(),
        [](const ServingWorkerGroup& group) {
            return group.role == ServingWorkerGroupRole::Expert;
        });
}

size_t ServingTopology::assign_replica(const ServingRequestSpec& request) const {
    const auto replicas = replica_count();
    if (replicas == 0) {
        return 0;
    }
    return static_cast<size_t>((request.request_id + request.original_index) %
                               replicas);
}

std::optional<size_t> ServingTopology::find_idle_group(
    ServingWorkerGroupRole role) const {
    for (const auto& group : worker_groups) {
        if (group.role == role && !group.busy) {
            return group.group_id;
        }
    }
    return std::nullopt;
}

std::optional<size_t> ServingTopology::find_idle_group_for_replica(
    ServingWorkerGroupRole role,
    size_t replica_id) const {
    for (const auto& group : worker_groups) {
        if (group.role == role && group.replica_id == replica_id &&
            !group.busy) {
            return group.group_id;
        }
    }
    return std::nullopt;
}

std::vector<size_t> ServingTopology::group_ids_for_role(
    ServingWorkerGroupRole role) const {
    std::vector<size_t> group_ids;
    for (const auto& group : worker_groups) {
        if (group.role == role) {
            group_ids.push_back(group.group_id);
        }
    }
    return group_ids;
}

std::vector<size_t> ServingTopology::group_ids_for_role(
    ServingWorkerGroupRole role,
    size_t replica_id) const {
    std::vector<size_t> group_ids;
    for (const auto& group : worker_groups) {
        if (group.role == role && group.replica_id == replica_id) {
            group_ids.push_back(group.group_id);
        }
    }
    return group_ids;
}

void ServingTopology::mark_group_busy(size_t group_id, uint64_t batch_id) {
    worker_groups.at(group_id).busy = true;
    worker_groups.at(group_id).active_batch_id = batch_id;
}

void ServingTopology::mark_group_idle(size_t group_id) {
    worker_groups.at(group_id).busy = false;
    worker_groups.at(group_id).active_batch_id = 0;
}

const ParallelismLayoutSpec& ServingTopology::colocated_layout() const {
    return topology_spec.colocated_layout;
}

const ParallelismLayoutSpec& ServingTopology::prefill_layout() const {
    return effective_prefill_layout(topology_spec);
}

const ParallelismLayoutSpec& ServingTopology::decode_layout() const {
    return effective_decode_layout(topology_spec);
}

}  // namespace AstraSim
