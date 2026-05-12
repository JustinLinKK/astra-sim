#ifndef __PD_SERVING_RUNTIME_HH__
#define __PD_SERVING_RUNTIME_HH__

#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

#include "astra-sim/workload/ServingRuntime.hh"

namespace AstraSim {

class PdServingRuntime : public ServingRuntimeBase {
  public:
    explicit PdServingRuntime(ServingRuntimeContext context);

    void fire() override;
    void call(EventType event, CallData* data) override;

  private:
    void handle_request_arrival(size_t request_index);
    void try_schedule_prefill();
    void try_schedule_transfer();
    void try_schedule_decode();
    std::optional<ServingBatch> build_prefill_batch(size_t worker_id);
    std::optional<ServingBatch> build_decode_batch(size_t worker_id);
    void schedule_batch(ServingBatch batch);
    void schedule_transfer(size_t request_index);
    void complete_batch(uint64_t batch_id);
    void complete_prefill_batch(const ServingBatch& batch);
    void complete_decode_batch(const ServingBatch& batch);
    void complete_transfer(uint64_t batch_id);

    std::vector<std::deque<size_t>> prefill_queues;
    std::deque<size_t> transfer_queue;
    std::vector<std::deque<size_t>> decode_queues;
    std::unordered_map<uint64_t, ServingBatch> active_batches;
};

}  // namespace AstraSim

#endif /* __PD_SERVING_RUNTIME_HH__ */
