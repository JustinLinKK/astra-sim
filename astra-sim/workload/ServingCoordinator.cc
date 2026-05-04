#include "astra-sim/workload/ServingCoordinator.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <json/json.hpp>
#include <sstream>
#include <stdexcept>

#include "astra-sim/common/Logging.hh"
#include "astra-sim/system/IntData.hh"

#ifndef ASTRA_SIM_GIT_COMMIT
#define ASTRA_SIM_GIT_COMMIT "unknown"
#endif

#ifndef ASTRA_SIM_VERSION
#define ASTRA_SIM_VERSION "serving-week2-v1"
#endif

using json = nlohmann::json;

namespace AstraSim {

namespace {

[[noreturn]] void serving_runtime_error(const std::string& message) {
    LoggerFactory::get_logger("serving")->critical(message);
    throw std::runtime_error(message);
}

bool has_output_path(const std::string& path) {
    return !path.empty() && path != "empty";
}

json stats_to_json(const ServingMetricStats& stats) {
    return json{
        {"count", stats.count},
        {"mean", stats.mean},
        {"p50", stats.p50},
        {"p90", stats.p90},
        {"p99", stats.p99},
        {"max", stats.max},
    };
}

std::string make_timestamp_utc() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc_tm{};
    gmtime_r(&time, &utc_tm);

    std::ostringstream stream;
    stream << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

}  // namespace

ServingCoordinator::ServingCoordinator(
    const std::vector<Sys*>& systems,
    const std::string& request_configuration_path,
    const std::string& request_metrics_output,
    const std::string& request_summary_output,
    const std::string& request_run_metadata_output,
    const std::string& binary_name,
    double compute_scale,
    double comm_scale)
    : systems(systems),
      control_sys(systems.empty() ? nullptr : systems.front()),
      config(ServingConfig::load_from_file(request_configuration_path)),
      request_configuration_path(request_configuration_path),
      request_metrics_output(request_metrics_output),
      request_summary_output(request_summary_output),
      request_run_metadata_output(request_run_metadata_output),
      binary_name(binary_name),
      compute_scale(compute_scale),
      comm_scale(comm_scale),
      finalized(false),
      request_active(false),
      active_request_index(0),
      pending_collective_completions(0),
      pending_collective_continuation(CollectiveContinuation::None),
      pending_collective_token_index(0),
      pending_collective_request_index(0) {
    if (this->control_sys == nullptr) {
        serving_runtime_error(
            "Serving coordinator requires at least one system instance");
    }

    this->requests.reserve(config.requests.size());
    for (const auto& request_spec : config.requests) {
        this->requests.push_back(
            ServingRequestState{request_spec, 0, 0, 0, 0, 0, false, false});
    }
    std::stable_sort(this->requests.begin(), this->requests.end(),
                     [](const ServingRequestState& lhs,
                        const ServingRequestState& rhs) {
                         return lhs.spec.arrival_time_ns <
                                rhs.spec.arrival_time_ns;
                     });
}

void ServingCoordinator::fire() {
    schedule_request_arrivals();
    if (requests.empty()) {
        finalize();
    }
}

void ServingCoordinator::schedule_request_arrivals() {
    for (size_t request_index = 0; request_index < requests.size();
         ++request_index) {
        if (requests[request_index].spec.arrival_time_ns == 0) {
            enqueue_request(request_index);
            continue;
        }
        control_sys->register_event(
            this, EventType::ServingRequestArrival,
            new ServingEventData(request_index,
                                 ServingEventData::Stage::RequestArrival, 0),
            requests[request_index].spec.arrival_time_ns);
    }
    try_start_next_request();
}

void ServingCoordinator::enqueue_request(size_t request_index) {
    pending_requests.push_back(request_index);
}

void ServingCoordinator::handle_stage_completion(
    size_t request_index,
    ServingEventData::Stage stage,
    uint64_t token_index) {
    if (stage == ServingEventData::Stage::PrefillCompute) {
        const auto scaled_collective_size =
            scale_collective_size(config.prefill_collective.size_bytes);
        if (config.prefill_collective.enabled && scaled_collective_size > 0) {
            start_collective(config.prefill_collective,
                             CollectiveContinuation::AfterPrefill,
                             request_index, 0);
        } else {
            mark_prefill_complete(request_index);
            start_decode_compute(request_index, 1);
        }
        return;
    }

    if (stage == ServingEventData::Stage::DecodeCompute) {
        const auto scaled_collective_size =
            scale_collective_size(config.decode_collective.size_bytes);
        if (config.decode_collective.enabled && scaled_collective_size > 0) {
            start_collective(config.decode_collective,
                             CollectiveContinuation::AfterDecode,
                             request_index, token_index);
        } else {
            complete_decode_token(request_index, token_index);
        }
        return;
    }

    serving_runtime_error("Unknown serving stage completion event");
}

void ServingCoordinator::call(EventType event, CallData* data) {
    if (event == EventType::ServingRequestArrival ||
        event == EventType::ServingStageCompleted) {
        auto* serving_data = static_cast<ServingEventData*>(data);
        const size_t request_index = serving_data->request_index;
        const auto stage = serving_data->stage;
        const auto token_index = serving_data->token_index;
        delete serving_data;

        if (event == EventType::ServingRequestArrival) {
            enqueue_request(request_index);
            try_start_next_request();
            return;
        }

        handle_stage_completion(request_index, stage, token_index);
        return;
    }

    if (event == EventType::ServingCollectiveCompleted) {
        auto* collective_data = static_cast<IntData*>(data);
        (void)collective_data;
        pending_collective_completions--;

        if (pending_collective_completions == 0) {
            const auto continuation = pending_collective_continuation;
            const auto request_index = pending_collective_request_index;
            const auto token_index = pending_collective_token_index;
            pending_collective_continuation = CollectiveContinuation::None;
            pending_collective_request_index = 0;
            pending_collective_token_index = 0;

            if (continuation == CollectiveContinuation::AfterPrefill) {
                mark_prefill_complete(request_index);
                start_decode_compute(request_index, 1);
                return;
            }
            if (continuation == CollectiveContinuation::AfterDecode) {
                complete_decode_token(request_index, token_index);
                return;
            }
            serving_runtime_error("Serving collective completed without a "
                                  "registered continuation");
        }
        return;
    }

    serving_runtime_error("Unsupported serving coordinator event");
}

void ServingCoordinator::try_start_next_request() {
    if (request_active || pending_requests.empty()) {
        return;
    }

    request_active = true;
    active_request_index = pending_requests.front();
    pending_requests.pop_front();

    auto& request = requests[active_request_index];
    request.service_start_ns = Sys::boostedTick();
    start_prefill_compute(active_request_index);
}

void ServingCoordinator::start_prefill_compute(size_t request_index) {
    const auto delay = scale_duration(config.prefill_base_latency_ns,
                                      requests[request_index].spec.prompt_tokens,
                                      config.prefill_compute_ns_per_token);
    if (delay == 0) {
        handle_stage_completion(request_index,
                                ServingEventData::Stage::PrefillCompute, 0);
        return;
    }

    control_sys->register_event(
        this, EventType::ServingStageCompleted,
        new ServingEventData(request_index,
                             ServingEventData::Stage::PrefillCompute, 0),
        delay);
}

void ServingCoordinator::start_decode_compute(size_t request_index,
                                              uint64_t token_index) {
    if (token_index == 1) {
        mark_prefill_complete(request_index);
    }

    const auto delay =
        scale_duration(token_index == 1 ? config.decode_base_latency_ns : 0,
                       1, config.decode_compute_ns_per_token);
    if (delay == 0) {
        handle_stage_completion(request_index,
                                ServingEventData::Stage::DecodeCompute,
                                token_index);
        return;
    }

    control_sys->register_event(
        this, EventType::ServingStageCompleted,
        new ServingEventData(request_index,
                             ServingEventData::Stage::DecodeCompute,
                             token_index),
        delay);
}

void ServingCoordinator::mark_prefill_complete(size_t request_index) {
    auto& request = requests[request_index];
    if (!request.prefill_complete_recorded) {
        request.prefill_end_time_ns = Sys::boostedTick();
        request.prefill_complete_recorded = true;
    }
}

void ServingCoordinator::start_collective(
    const ServingCollectiveSpec& collective,
    CollectiveContinuation continuation,
    size_t request_index,
    uint64_t token_index) {
    const auto scaled_collective_size =
        scale_collective_size(collective.size_bytes);
    if (scaled_collective_size == 0) {
        if (continuation == CollectiveContinuation::AfterPrefill) {
            mark_prefill_complete(request_index);
            start_decode_compute(request_index, 1);
        } else if (continuation == CollectiveContinuation::AfterDecode) {
            complete_decode_token(request_index, token_index);
        }
        return;
    }

    pending_collective_completions = static_cast<int>(systems.size());
    pending_collective_continuation = continuation;
    pending_collective_request_index = request_index;
    pending_collective_token_index = token_index;

    for (auto* sys : systems) {
        auto* dataset = issue_collective(sys, collective, scaled_collective_size);
        dataset->set_notifier(this, EventType::ServingCollectiveCompleted);
    }
}

void ServingCoordinator::complete_decode_token(size_t request_index,
                                               uint64_t token_index) {
    auto& request = requests[request_index];
    const auto now = Sys::boostedTick();

    request.completed_output_tokens = token_index;
    if (!request.first_token_recorded && token_index == 1) {
        request.first_token_time_ns = now;
        request.first_token_recorded = true;
    }

    if (token_index >= request.spec.output_tokens) {
        request.finish_time_ns = now;
        finish_request(request_index);
        return;
    }

    start_decode_compute(request_index, token_index + 1);
}

void ServingCoordinator::finish_request(size_t request_index) {
    auto& request = requests[request_index];
    ServingRequestMetrics metrics;
    metrics.request_id = request.spec.request_id;
    metrics.arrival_time_ns = request.spec.arrival_time_ns;
    metrics.prompt_tokens = request.spec.prompt_tokens;
    metrics.output_tokens = request.spec.output_tokens;
    metrics.service_start_ns = request.service_start_ns;
    metrics.prefill_end_time_ns = request.prefill_end_time_ns;
    metrics.first_token_time_ns = request.first_token_time_ns;
    metrics.finish_time_ns = request.finish_time_ns;
    metrics.queue_delay_ns =
        request.service_start_ns - request.spec.arrival_time_ns;
    metrics.prefill_duration_ns =
        request.prefill_end_time_ns - request.service_start_ns;
    metrics.decode_duration_ns =
        request.finish_time_ns - request.prefill_end_time_ns;
    metrics.ttft_ns = request.first_token_time_ns - request.spec.arrival_time_ns;
    metrics.tpot_ns = calculate_serving_tpot_ns(metrics.decode_duration_ns,
                                                request.spec.output_tokens);
    metrics.e2e_ns = request.finish_time_ns - request.spec.arrival_time_ns;

    completed_metrics.push_back(metrics);
    emit_request_metrics(metrics);

    request_active = false;
    if (completed_metrics.size() == requests.size()) {
        finalize();
        return;
    }
    try_start_next_request();
}

void ServingCoordinator::finalize() {
    if (finalized) {
        return;
    }
    finalized = true;

    std::vector<double> queue_delay_values;
    std::vector<double> ttft_values;
    std::vector<double> tpot_values;
    std::vector<double> e2e_values;
    queue_delay_values.reserve(completed_metrics.size());
    ttft_values.reserve(completed_metrics.size());
    tpot_values.reserve(completed_metrics.size());
    e2e_values.reserve(completed_metrics.size());

    Tick simulation_start_time_ns = 0;
    Tick simulation_end_time_ns = 0;
    uint64_t total_prompt_tokens = 0;
    uint64_t total_output_tokens = 0;
    if (!completed_metrics.empty()) {
        simulation_start_time_ns = completed_metrics.front().arrival_time_ns;
        simulation_end_time_ns = completed_metrics.front().finish_time_ns;
    }

    for (const auto& metrics : completed_metrics) {
        queue_delay_values.push_back(static_cast<double>(metrics.queue_delay_ns));
        ttft_values.push_back(static_cast<double>(metrics.ttft_ns));
        tpot_values.push_back(metrics.tpot_ns);
        e2e_values.push_back(static_cast<double>(metrics.e2e_ns));
        simulation_start_time_ns =
            std::min(simulation_start_time_ns, metrics.arrival_time_ns);
        simulation_end_time_ns =
            std::max(simulation_end_time_ns, metrics.finish_time_ns);
        total_prompt_tokens += metrics.prompt_tokens;
        total_output_tokens += metrics.output_tokens;
    }

    const auto makespan_ns = simulation_end_time_ns - simulation_start_time_ns;
    print_summary_line("QUEUE_DELAY",
                       summarize_serving_metric(queue_delay_values));
    print_summary_line("TTFT", summarize_serving_metric(ttft_values));
    print_summary_line("TPOT", summarize_serving_metric(tpot_values));
    print_summary_line("E2E", summarize_serving_metric(e2e_values));
    print_throughput_line(makespan_ns, total_output_tokens);

    if (has_output_path(request_metrics_output)) {
        write_metrics_csv();
    }
    if (has_output_path(request_summary_output)) {
        write_summary_json(queue_delay_values, ttft_values, tpot_values,
                           e2e_values, simulation_start_time_ns,
                           simulation_end_time_ns, makespan_ns,
                           total_prompt_tokens, total_output_tokens);
    }
    if (has_output_path(request_run_metadata_output)) {
        write_run_metadata_json();
    }
}

void ServingCoordinator::emit_request_metrics(
    const ServingRequestMetrics& metrics) const {
    std::ostringstream message;
    message << std::fixed << std::setprecision(2)
            << "REQUEST request_id=" << metrics.request_id
            << " arrival_time_ns=" << metrics.arrival_time_ns
            << " prompt_tokens=" << metrics.prompt_tokens
            << " output_tokens=" << metrics.output_tokens
            << " service_start_ns=" << metrics.service_start_ns
            << " prefill_end_time_ns=" << metrics.prefill_end_time_ns
            << " first_token_time_ns=" << metrics.first_token_time_ns
            << " finish_time_ns=" << metrics.finish_time_ns
            << " queue_delay_ns=" << metrics.queue_delay_ns
            << " prefill_duration_ns=" << metrics.prefill_duration_ns
            << " decode_duration_ns=" << metrics.decode_duration_ns
            << " ttft_ns=" << metrics.ttft_ns
            << " tpot_ns=" << metrics.tpot_ns
            << " e2e_ns=" << metrics.e2e_ns;
    LoggerFactory::get_logger("serving")->info(message.str());
}

void ServingCoordinator::write_metrics_csv() const {
    std::ofstream output(request_metrics_output);
    if (!output.is_open()) {
        serving_runtime_error("Unable to open request metrics output file: " +
                              request_metrics_output);
    }

    output << "request_id,arrival_time_ns,prompt_tokens,output_tokens,"
              "service_start_ns,prefill_end_time_ns,first_token_time_ns,"
              "finish_time_ns,queue_delay_ns,prefill_duration_ns,"
              "decode_duration_ns,ttft_ns,tpot_ns,e2e_ns\n";

    output << std::fixed << std::setprecision(2);
    for (const auto& metrics : completed_metrics) {
        output << metrics.request_id << "," << metrics.arrival_time_ns << ","
               << metrics.prompt_tokens << "," << metrics.output_tokens << ","
               << metrics.service_start_ns << ","
               << metrics.prefill_end_time_ns << ","
               << metrics.first_token_time_ns << "," << metrics.finish_time_ns
               << "," << metrics.queue_delay_ns << ","
               << metrics.prefill_duration_ns << ","
               << metrics.decode_duration_ns << "," << metrics.ttft_ns << ","
               << metrics.tpot_ns << "," << metrics.e2e_ns << "\n";
    }
}

void ServingCoordinator::write_summary_json(
    const std::vector<double>& queue_delay_values,
    const std::vector<double>& ttft_values,
    const std::vector<double>& tpot_values,
    const std::vector<double>& e2e_values,
    Tick simulation_start_time_ns,
    Tick simulation_end_time_ns,
    Tick makespan_ns,
    uint64_t total_prompt_tokens,
    uint64_t total_output_tokens) const {
    std::ofstream output(request_summary_output);
    if (!output.is_open()) {
        serving_runtime_error("Unable to open request summary output file: " +
                              request_summary_output);
    }

    json summary{
        {"num_requests", completed_metrics.size()},
        {"total_prompt_tokens", total_prompt_tokens},
        {"total_output_tokens", total_output_tokens},
        {"simulation_start_time_ns", simulation_start_time_ns},
        {"simulation_end_time_ns", simulation_end_time_ns},
        {"makespan_ns", makespan_ns},
        {"request_throughput_reqs_per_sec",
         calculate_throughput_per_second(completed_metrics.size(), makespan_ns)},
        {"output_token_throughput_tokens_per_sec",
         calculate_throughput_per_second(total_output_tokens, makespan_ns)},
        {"queue_delay_ns",
         stats_to_json(summarize_serving_metric(queue_delay_values))},
        {"ttft_ns", stats_to_json(summarize_serving_metric(ttft_values))},
        {"tpot_ns", stats_to_json(summarize_serving_metric(tpot_values))},
        {"e2e_ns", stats_to_json(summarize_serving_metric(e2e_values))},
    };

    output << summary.dump(2) << "\n";
}

void ServingCoordinator::write_run_metadata_json() const {
    std::ofstream output(request_run_metadata_output);
    if (!output.is_open()) {
        serving_runtime_error(
            "Unable to open request run metadata output file: " +
            request_run_metadata_output);
    }

    json metadata{
        {"timestamp_utc", make_timestamp_utc()},
        {"git_commit", ASTRA_SIM_GIT_COMMIT},
        {"config_path", request_configuration_path},
        {"seed", config.trace_seed ? json(*config.trace_seed) : json(nullptr)},
        {"binary", binary_name},
        {"simulator_version", ASTRA_SIM_VERSION},
    };

    output << metadata.dump(2) << "\n";
}

void ServingCoordinator::print_summary_line(
    const std::string& metric_name,
    const ServingMetricStats& stats) const {
    std::ostringstream message;
    message << std::fixed << std::setprecision(2)
            << "SUMMARY metric=" << metric_name
            << " count=" << stats.count
            << " avg_ns=" << stats.mean
            << " p50_ns=" << stats.p50
            << " p90_ns=" << stats.p90
            << " p99_ns=" << stats.p99
            << " max_ns=" << stats.max;
    LoggerFactory::get_logger("serving")->info(message.str());
}

void ServingCoordinator::print_throughput_line(
    Tick makespan_ns,
    uint64_t total_output_tokens) const {
    std::ostringstream message;
    message << std::fixed << std::setprecision(6)
            << "THROUGHPUT total_requests=" << completed_metrics.size()
            << " total_output_tokens=" << total_output_tokens
            << " makespan_ns=" << makespan_ns
            << " request_throughput_reqs_per_sec="
            << calculate_throughput_per_second(completed_metrics.size(),
                                               makespan_ns)
            << " output_token_throughput_tokens_per_sec="
            << calculate_throughput_per_second(total_output_tokens,
                                               makespan_ns);
    LoggerFactory::get_logger("serving")->info(message.str());
}

DataSet* ServingCoordinator::issue_collective(
    Sys* sys,
    const ServingCollectiveSpec& collective,
    uint64_t size_bytes) const {
    std::vector<bool> involved_dimensions(sys->physical_dims.size(), true);
    switch (collective.type) {
    case ComType::All_Reduce:
        return sys->generate_all_reduce(size_bytes, involved_dimensions,
                                        nullptr, 0);
    case ComType::All_Gather:
        return sys->generate_all_gather(size_bytes, involved_dimensions,
                                        nullptr, 0);
    case ComType::Reduce_Scatter:
        return sys->generate_reduce_scatter(size_bytes, involved_dimensions,
                                            nullptr, 0);
    case ComType::All_to_All:
        return sys->generate_all_to_all(size_bytes, involved_dimensions,
                                        nullptr, 0);
    default:
        serving_runtime_error("Unsupported serving collective type at runtime");
        return nullptr;
    }
}

Tick ServingCoordinator::scale_duration(Tick base_delay,
                                        uint64_t units,
                                        Tick per_unit_delay) const {
    return calculate_scaled_serving_duration(base_delay, units, per_unit_delay,
                                             compute_scale);
}

uint64_t ServingCoordinator::scale_collective_size(uint64_t size_bytes) const {
    if (size_bytes == 0) {
        return 0;
    }
    const auto scaled_size =
        std::llround(static_cast<long double>(size_bytes) * comm_scale);
    return scaled_size > 0 ? static_cast<uint64_t>(scaled_size) : 0;
}

}  // namespace AstraSim
