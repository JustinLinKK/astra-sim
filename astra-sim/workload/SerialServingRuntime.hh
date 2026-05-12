#ifndef __SERIAL_SERVING_RUNTIME_HH__
#define __SERIAL_SERVING_RUNTIME_HH__

#include <deque>

#include "astra-sim/workload/ServingRuntime.hh"

namespace AstraSim {

class SerialServingRuntime : public ServingRuntimeBase {
  public:
    explicit SerialServingRuntime(ServingRuntimeContext context);

    void fire() override;
    void call(EventType event, CallData* data) override;

  private:
    enum class CollectiveContinuation {
        None = 0,
        AfterPrefill,
        AfterDecode,
    };

    void enqueue_request(size_t request_index);
    void try_start_next_request();
    void start_prefill_compute(size_t request_index);
    void start_decode_compute(size_t request_index, uint64_t token_index);
    void handle_stage_completion(size_t request_index,
                                 ServingStageType stage,
                                 uint64_t token_index);
    void start_collective(const ServingCollectiveSpec& collective,
                          CollectiveContinuation continuation,
                          size_t request_index,
                          uint64_t token_index);
    void complete_decode_token(size_t request_index, uint64_t token_index);

    std::deque<size_t> pending_requests;
    bool request_active;
    size_t active_request_index;
    int pending_collective_completions;
    CollectiveContinuation pending_collective_continuation;
    size_t pending_collective_request_index;
    uint64_t pending_collective_token_index;
};

}  // namespace AstraSim

#endif /* __SERIAL_SERVING_RUNTIME_HH__ */
