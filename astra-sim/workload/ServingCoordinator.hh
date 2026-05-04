#ifndef __SERVING_COORDINATOR_HH__
#define __SERVING_COORDINATOR_HH__

#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "astra-sim/system/Callable.hh"
#include "astra-sim/system/DataSet.hh"
#include "astra-sim/system/Sys.hh"
#include "astra-sim/workload/ServingConfig.hh"
#include "astra-sim/workload/ServingUtils.hh"

namespace AstraSim {

struct ServingRequestState {
    ServingRequestSpec spec;
    Tick service_start_ns;
    Tick prefill_end_time_ns;
    Tick first_token_time_ns;
    Tick finish_time_ns;
    uint64_t completed_output_tokens;
    bool prefill_complete_recorded;
    bool first_token_recorded;
};

struct ServingRequestMetrics {
    uint64_t request_id;
    Tick arrival_time_ns;
    uint64_t prompt_tokens;
    uint64_t output_tokens;
    Tick service_start_ns;
    Tick prefill_end_time_ns;
    Tick first_token_time_ns;
    Tick finish_time_ns;
    Tick queue_delay_ns;
    Tick prefill_duration_ns;
    Tick decode_duration_ns;
    Tick ttft_ns;
    double tpot_ns;
    Tick e2e_ns;
};

class ServingEventData : public CallData {
  public:
    enum class Stage {
        RequestArrival = 0,
        PrefillCompute,
        DecodeCompute,
    };

    ServingEventData(size_t request_index, Stage stage, uint64_t token_index)
        : request_index(request_index),
          stage(stage),
          token_index(token_index) {}

    size_t request_index;
    Stage stage;
    uint64_t token_index;
};

class ServingCoordinator : public Callable {
  public:
    ServingCoordinator(const std::vector<Sys*>& systems,
                       const std::string& request_configuration_path,
                       const std::string& request_metrics_output,
                       const std::string& request_summary_output,
                       const std::string& request_run_metadata_output,
                       const std::string& binary_name,
                       double compute_scale,
                       double comm_scale);

    void fire();
    void call(EventType event, CallData* data) override;

  private:
    enum class CollectiveContinuation {
        None = 0,
        AfterPrefill,
        AfterDecode,
    };

    void schedule_request_arrivals();
    void enqueue_request(size_t request_index);
    void handle_stage_completion(size_t request_index,
                                 ServingEventData::Stage stage,
                                 uint64_t token_index);
    void try_start_next_request();
    void start_prefill_compute(size_t request_index);
    void start_decode_compute(size_t request_index, uint64_t token_index);
    void mark_prefill_complete(size_t request_index);
    void start_collective(const ServingCollectiveSpec& collective,
                          CollectiveContinuation continuation,
                          size_t request_index,
                          uint64_t token_index);
    void complete_decode_token(size_t request_index, uint64_t token_index);
    void finish_request(size_t request_index);
    void finalize();
    void emit_request_metrics(const ServingRequestMetrics& metrics) const;
    void write_metrics_csv() const;
    void write_summary_json(const std::vector<double>& queue_delay_values,
                            const std::vector<double>& ttft_values,
                            const std::vector<double>& tpot_values,
                            const std::vector<double>& e2e_values,
                            Tick simulation_start_time_ns,
                            Tick simulation_end_time_ns,
                            Tick makespan_ns,
                            uint64_t total_prompt_tokens,
                            uint64_t total_output_tokens) const;
    void write_run_metadata_json() const;
    void print_summary_line(const std::string& metric_name,
                            const ServingMetricStats& stats) const;
    void print_throughput_line(Tick makespan_ns,
                               uint64_t total_output_tokens) const;
    DataSet* issue_collective(Sys* sys,
                              const ServingCollectiveSpec& collective,
                              uint64_t size_bytes) const;
    Tick scale_duration(Tick base_delay,
                        uint64_t units,
                        Tick per_unit_delay) const;
    uint64_t scale_collective_size(uint64_t size_bytes) const;

    std::vector<Sys*> systems;
    Sys* control_sys;
    ServingConfig config;
    std::vector<ServingRequestState> requests;
    std::deque<size_t> pending_requests;
    std::vector<ServingRequestMetrics> completed_metrics;
    std::string request_configuration_path;
    std::string request_metrics_output;
    std::string request_summary_output;
    std::string request_run_metadata_output;
    std::string binary_name;
    double compute_scale;
    double comm_scale;
    bool finalized;
    bool request_active;
    size_t active_request_index;
    int pending_collective_completions;
    CollectiveContinuation pending_collective_continuation;
    uint64_t pending_collective_token_index;
    size_t pending_collective_request_index;
};

}  // namespace AstraSim

#endif /* __SERVING_COORDINATOR_HH__ */
