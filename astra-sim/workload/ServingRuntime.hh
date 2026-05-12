#ifndef __SERVING_RUNTIME_HH__
#define __SERVING_RUNTIME_HH__

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "astra-sim/system/Callable.hh"
#include "astra-sim/system/DataSet.hh"
#include "astra-sim/system/Sys.hh"
#include "astra-sim/workload/ServingConfig.hh"
#include "astra-sim/workload/ServingCostModel.hh"
#include "astra-sim/workload/ServingMetrics.hh"
#include "astra-sim/workload/ServingTypes.hh"
#include "astra-sim/workload/ServingUtils.hh"
#include "astra-sim/workload/serving/topology/ServingTopology.hh"

namespace AstraSim {

struct ServingRequestState {
    ServingRequestSpec spec;
    RequestPhase phase = RequestPhase::Arrived;
    Tick arrival_time_ns = 0;
    Tick service_start_ns = 0;
    Tick prefill_queue_enter_ns = 0;
    Tick prefill_start_ns = 0;
    Tick prefill_end_time_ns = 0;
    Tick transfer_queue_enter_ns = 0;
    Tick transfer_start_ns = 0;
    Tick transfer_end_ns = 0;
    Tick decode_queue_enter_ns = 0;
    Tick decode_start_ns = 0;
    Tick first_token_time_ns = 0;
    Tick finish_time_ns = 0;
    uint64_t remaining_prefill_tokens = 0;
    uint64_t completed_prefill_tokens = 0;
    uint64_t completed_output_tokens = 0;
    Tick accumulated_prefill_compute_ns = 0;
    Tick accumulated_decode_compute_ns = 0;
    Tick accumulated_transfer_ns = 0;
    Tick accumulated_collective_ns = 0;
    size_t replica_id = 0;
    std::optional<size_t> prefill_group_id;
    std::optional<size_t> decode_group_id;
    uint64_t kv_transfer_bytes = 0;
    uint64_t kv_resident_bytes = 0;
    ServingStageBreakdown accumulated_prefill_breakdown;
    ServingStageBreakdown accumulated_decode_breakdown;
    ServingStageBreakdown accumulated_transfer_breakdown;
    bool prefill_queue_recorded = false;
    bool service_started_recorded = false;
    bool prefill_started_recorded = false;
    bool transfer_queue_recorded = false;
    bool transfer_started_recorded = false;
    bool decode_queue_recorded = false;
    bool decode_started_recorded = false;
    bool prefill_complete_recorded = false;
    bool first_token_recorded = false;
    bool finished_recorded = false;
    bool in_active_batch = false;
};

struct ServingRuntimeContext {
    std::vector<Sys*> systems;
    Sys* control_sys = nullptr;
    Callable* event_sink = nullptr;
    const ServingConfig* config = nullptr;
    ServingOutputPaths outputs;
    std::string binary_name;
    double compute_scale = 1.0;
    double comm_scale = 1.0;
};

class ServingRuntime {
  public:
    virtual ~ServingRuntime() = default;
    virtual void fire() = 0;
    virtual void call(EventType event, CallData* data) = 0;
};

class ServingRuntimeBase : public ServingRuntime {
  public:
    explicit ServingRuntimeBase(ServingRuntimeContext context);

  protected:
    bool has_output_path(const std::string& path) const;
    std::vector<size_t> sorted_request_indices() const;
    void schedule_request_arrivals();
    void mark_service_start(size_t request_index);
    void mark_prefill_queue_enter(size_t request_index, Tick when);
    void mark_prefill_start(size_t request_index, Tick when);
    void mark_prefill_end(size_t request_index, Tick when);
    void mark_transfer_queue_enter(size_t request_index, Tick when);
    void mark_transfer_start(size_t request_index, Tick when);
    void mark_transfer_end(size_t request_index, Tick when);
    void mark_decode_queue_enter(size_t request_index, Tick when);
    void mark_decode_start(size_t request_index, Tick when);
    void mark_first_token(size_t request_index, Tick when);
    void mark_request_finished(size_t request_index, Tick when);
    void add_prefill_runtime(size_t request_index, Tick duration);
    void add_decode_runtime(size_t request_index, Tick duration);
    void add_transfer_runtime(size_t request_index, Tick duration);
    void add_collective_runtime(size_t request_index, Tick duration);
    void add_prefill_breakdown(size_t request_index,
                               const ServingStageBreakdown& breakdown);
    void add_decode_breakdown(size_t request_index,
                              const ServingStageBreakdown& breakdown);
    void add_transfer_breakdown(size_t request_index,
                                const ServingStageBreakdown& breakdown);
    void record_stage_schedule(const ServingBatch& batch,
                               const std::string& event_name);
    void record_request_event(size_t request_index, const std::string& event_name);
    ServingRequestMetrics build_request_metrics(const ServingRequestState& request) const;
    void complete_request(size_t request_index);
    void finalize_if_done();
    void finalize();
    DataSet* issue_collective(Sys* sys,
                              const ServingCollectiveSpec& collective,
                              uint64_t size_bytes) const;

    ServingRuntimeContext context;
    std::vector<Sys*> systems;
    Sys* control_sys;
    Callable* event_sink;
    const ServingConfig& config;
    ServingCostModel cost_model;
    ServingTopology topology;
    std::vector<ServingRequestState> requests;
    std::vector<size_t> arrival_order;
    std::vector<ServingRequestMetrics> completed_metrics;
    std::vector<ServingEventTraceRecord> event_trace_records;
    bool finalized;
    uint64_t next_batch_id;
};

std::unique_ptr<ServingRuntime> create_serving_runtime(
    ServingRuntimeContext context);

}  // namespace AstraSim

#endif /* __SERVING_RUNTIME_HH__ */
