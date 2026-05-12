#ifndef __SERVING_TYPES_HH__
#define __SERVING_TYPES_HH__

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "astra-sim/common/ParallelismLayout.hh"
#include "astra-sim/system/CallData.hh"
#include "astra-sim/system/Common.hh"

namespace AstraSim {

enum class ServingArchitecture {
    SerialBaseline = 0,
    Colocated,
    ColocatedChunked,
    PdDisaggregated,
};

enum class ServingSchedulerPolicy {
    Serial = 0,
    DecodeFirst,
    PrefillFirst,
    Balanced,
};

enum class RequestPhase {
    Arrived = 0,
    WaitingForAdmission,
    PrefillQueued,
    PrefillRunning,
    TransferQueued,
    TransferRunning,
    DecodeQueued,
    DecodeRunning,
    Finished,
};

enum class ServingStageType {
    RequestArrival = 0,
    SerialPrefill,
    SerialDecode,
    ColocatedPrefill,
    ColocatedDecode,
    PdPrefill,
    PdTransfer,
    PdDecode,
};

enum class ServingWorkerRole {
    Colocated = 0,
    Prefill,
    Decode,
};

enum class ServingWorkerGroupRole {
    Colocated = 0,
    Prefill,
    Decode,
    Expert,
};

struct ServingStageBreakdown {
    Tick base_latency_ns = 0;
    Tick attention_compute_ns = 0;
    Tick ffn_or_expert_compute_ns = 0;
    Tick tp_collective_ns = 0;
    Tick pp_activation_ns = 0;
    Tick ep_dispatch_ns = 0;
    Tick dp_attention_sync_ns = 0;
    Tick pd_kv_transfer_ns = 0;

    Tick total_ns() const {
        return base_latency_ns + attention_compute_ns +
               ffn_or_expert_compute_ns + tp_collective_ns +
               pp_activation_ns + ep_dispatch_ns + dp_attention_sync_ns +
               pd_kv_transfer_ns;
    }
};

struct ServingBatchItem {
    size_t request_index = 0;
    uint64_t tokens = 0;
};

struct ServingBatch {
    uint64_t batch_id = 0;
    ServingStageType stage = ServingStageType::RequestArrival;
    size_t worker_id = 0;
    size_t worker_group_id = 0;
    size_t replica_id = 0;
    std::vector<ServingBatchItem> items;
    Tick scheduled_at_ns = 0;
    Tick duration_ns = 0;
    uint64_t sequence_length_hint = 0;
    bool include_base_latency = false;
    std::string layout_name;
    ParallelismLayoutSpec layout;
    ServingStageBreakdown breakdown;
};

struct ServingWorker {
    size_t worker_id = 0;
    ServingWorkerRole role = ServingWorkerRole::Colocated;
    bool busy = false;
    uint64_t active_batch_id = 0;
    std::vector<size_t> resident_requests;
};

struct ServingWorkerGroup {
    size_t group_id = 0;
    size_t replica_id = 0;
    ServingWorkerGroupRole role = ServingWorkerGroupRole::Colocated;
    ParallelismLayoutSpec layout;
    std::string name;
    size_t worker_count = 1;
    bool busy = false;
    uint64_t active_batch_id = 0;
    std::vector<size_t> resident_requests;
};

class ServingRuntimeEventData : public CallData {
  public:
    ServingRuntimeEventData(ServingStageType stage,
                            size_t request_index,
                            uint64_t batch_id = 0,
                            size_t worker_id = 0,
                            uint64_t token_index = 0)
        : stage(stage),
          request_index(request_index),
          batch_id(batch_id),
          worker_id(worker_id),
          token_index(token_index) {}

    ServingStageType stage;
    size_t request_index;
    uint64_t batch_id;
    size_t worker_id;
    uint64_t token_index;
};

std::string to_string(ServingArchitecture architecture);
std::string to_string(ServingSchedulerPolicy policy);
std::string to_string(RequestPhase phase);
std::string to_string(ServingStageType stage);
std::string to_string(ServingWorkerRole role);
std::string to_string(ServingWorkerGroupRole role);

}  // namespace AstraSim

#endif /* __SERVING_TYPES_HH__ */
