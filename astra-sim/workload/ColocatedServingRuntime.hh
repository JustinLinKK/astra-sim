#ifndef __COLOCATED_SERVING_RUNTIME_HH__
#define __COLOCATED_SERVING_RUNTIME_HH__

#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

#include "astra-sim/workload/ServingRuntime.hh"

namespace AstraSim {

class ColocatedServingRuntime : public ServingRuntimeBase {
  public:
    explicit ColocatedServingRuntime(ServingRuntimeContext context);

    void fire() override;
    void call(EventType event, CallData* data) override;

  private:
    void handle_request_arrival(size_t request_index);
    void admit_waiting_requests(size_t replica_id);
    void try_schedule();
    std::optional<ServingBatch> build_next_batch(size_t group_id);
    std::optional<ServingBatch> build_decode_batch(size_t group_id);
    std::optional<ServingBatch> build_prefill_batch(size_t group_id);
    void schedule_batch(ServingBatch batch);
    void complete_batch(uint64_t batch_id);
    bool has_prefill_work(size_t replica_id) const;
    bool has_decode_work(size_t replica_id) const;
    void reset_balanced_budget(size_t group_id);
    void complete_prefill_batch(const ServingBatch& batch);
    void complete_decode_batch(const ServingBatch& batch);
    size_t max_running_requests_per_replica() const;
    size_t running_request_count(size_t replica_id) const;
    size_t prefill_queue_depth(size_t replica_id) const;
    size_t decode_queue_depth(size_t replica_id) const;
    void annotate_batch_snapshot(ServingBatch* batch) const;

    std::vector<std::deque<size_t>> waiting_requests_by_replica;
    std::vector<std::vector<size_t>> running_requests_by_replica;
    std::unordered_map<uint64_t, ServingBatch> active_batches;
    std::unordered_map<size_t, uint64_t> balanced_decode_budget_remaining;
    bool chunking_enabled;
};

}  // namespace AstraSim

#endif /* __COLOCATED_SERVING_RUNTIME_HH__ */
