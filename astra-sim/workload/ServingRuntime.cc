#include "astra-sim/workload/ServingRuntime.hh"

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include "astra-sim/common/Logging.hh"
#include "astra-sim/workload/ColocatedServingRuntime.hh"
#include "astra-sim/workload/PdServingRuntime.hh"
#include "astra-sim/workload/SerialServingRuntime.hh"

namespace AstraSim {

namespace {

[[noreturn]] void serving_runtime_error(const std::string& message) {
    LoggerFactory::get_logger("serving")->critical(message);
    throw std::runtime_error(message);
}

std::string join_request_ids(const std::vector<ServingBatchItem>& items,
                             const std::vector<ServingRequestState>& requests) {
    std::ostringstream stream;
    for (size_t index = 0; index < items.size(); ++index) {
        if (index > 0) {
            stream << ";";
        }
        stream << requests[items[index].request_index].spec.request_id;
    }
    return stream.str();
}

uint64_t total_tokens(const ServingBatch& batch) {
    uint64_t total = 0;
    for (const auto& item : batch.items) {
        total += item.tokens;
    }
    return total;
}

}  // namespace

ServingRuntimeBase::ServingRuntimeBase(ServingRuntimeContext context)
    : context(std::move(context)),
      systems(this->context.systems),
      control_sys(this->context.control_sys),
      event_sink(this->context.event_sink),
      config(*this->context.config),
      cost_model(config, this->context.compute_scale, this->context.comm_scale),
      topology(config, systems.size()),
      finalized(false),
      next_batch_id(1) {
    requests.reserve(config.requests.size());
    for (const auto& request_spec : config.requests) {
        ServingRequestState state;
        state.spec = request_spec;
        state.arrival_time_ns = request_spec.arrival_time_ns;
        state.remaining_prefill_tokens = request_spec.prompt_tokens;
        state.replica_id = topology.assign_replica(request_spec);
        if (config.runtime.architecture == ServingArchitecture::PdDisaggregated) {
            state.kv_transfer_bytes = cost_model.estimate_kv_transfer_bytes(state);
        }
        requests.push_back(state);
        arrival_order.push_back(request_spec.original_index);
    }
    std::sort(arrival_order.begin(), arrival_order.end(),
              [this](size_t lhs, size_t rhs) {
                  const auto& lhs_request = requests[lhs].spec;
                  const auto& rhs_request = requests[rhs].spec;
                  if (lhs_request.arrival_time_ns != rhs_request.arrival_time_ns) {
                      return lhs_request.arrival_time_ns <
                             rhs_request.arrival_time_ns;
                  }
                  return lhs_request.original_index <
                         rhs_request.original_index;
              });
}

bool ServingRuntimeBase::has_output_path(const std::string& path) const {
    return !path.empty() && path != "empty";
}

std::vector<size_t> ServingRuntimeBase::sorted_request_indices() const {
    auto indices = arrival_order;
    std::sort(indices.begin(), indices.end(),
              [this](size_t lhs, size_t rhs) {
                  return requests[lhs].spec.original_index <
                         requests[rhs].spec.original_index;
              });
    return indices;
}

void ServingRuntimeBase::schedule_request_arrivals() {
    for (const auto request_index : arrival_order) {
        const auto arrival_time = requests[request_index].spec.arrival_time_ns;
        if (arrival_time == 0) {
            event_sink->call(
                EventType::ServingRequestArrival,
                new ServingRuntimeEventData(
                    ServingStageType::RequestArrival, request_index));
            continue;
        }
        control_sys->register_event(
            event_sink, EventType::ServingRequestArrival,
            new ServingRuntimeEventData(ServingStageType::RequestArrival,
                                        request_index),
            arrival_time);
    }
}

void ServingRuntimeBase::mark_service_start(size_t request_index) {
    auto& request = requests.at(request_index);
    if (!request.service_started_recorded) {
        request.service_start_ns = Sys::boostedTick();
        request.service_started_recorded = true;
    }
}

void ServingRuntimeBase::mark_prefill_queue_enter(size_t request_index,
                                                  Tick when) {
    auto& request = requests.at(request_index);
    if (!request.prefill_queue_recorded) {
        request.prefill_queue_enter_ns = when;
        request.prefill_queue_recorded = true;
    }
    request.latest_prefill_queue_enter_ns = when;
    request.prefill_queue_pending = true;
}

void ServingRuntimeBase::mark_prefill_start(size_t request_index, Tick when) {
    auto& request = requests.at(request_index);
    mark_service_start(request_index);
    if (request.prefill_queue_pending) {
        request.accumulated_prefill_queue_wait_ns +=
            when - request.latest_prefill_queue_enter_ns;
        request.prefill_queue_pending = false;
    }
    if (!request.prefill_started_recorded) {
        request.prefill_start_ns = when;
        request.prefill_started_recorded = true;
    }
}

void ServingRuntimeBase::mark_prefill_end(size_t request_index, Tick when) {
    auto& request = requests.at(request_index);
    if (!request.prefill_complete_recorded) {
        request.prefill_end_time_ns = when;
        request.prefill_complete_recorded = true;
    }
}

void ServingRuntimeBase::mark_transfer_queue_enter(size_t request_index,
                                                   Tick when) {
    auto& request = requests.at(request_index);
    if (!request.transfer_queue_recorded) {
        request.transfer_queue_enter_ns = when;
        request.transfer_queue_recorded = true;
    }
    request.latest_transfer_queue_enter_ns = when;
    request.transfer_queue_pending = true;
}

void ServingRuntimeBase::mark_transfer_start(size_t request_index, Tick when) {
    auto& request = requests.at(request_index);
    if (request.transfer_queue_pending) {
        request.accumulated_transfer_queue_wait_ns +=
            when - request.latest_transfer_queue_enter_ns;
        request.transfer_queue_pending = false;
    }
    if (!request.transfer_started_recorded) {
        request.transfer_start_ns = when;
        request.transfer_started_recorded = true;
    }
}

void ServingRuntimeBase::mark_transfer_end(size_t request_index, Tick when) {
    requests.at(request_index).transfer_end_ns = when;
}

void ServingRuntimeBase::mark_decode_queue_enter(size_t request_index,
                                                 Tick when) {
    auto& request = requests.at(request_index);
    if (!request.decode_queue_recorded) {
        request.decode_queue_enter_ns = when;
        request.decode_queue_recorded = true;
    }
    request.latest_decode_queue_enter_ns = when;
    request.decode_queue_pending = true;
}

void ServingRuntimeBase::mark_decode_start(size_t request_index, Tick when) {
    auto& request = requests.at(request_index);
    if (request.decode_queue_pending) {
        request.accumulated_decode_queue_wait_ns +=
            when - request.latest_decode_queue_enter_ns;
        request.decode_queue_pending = false;
    }
    if (!request.decode_started_recorded) {
        request.decode_start_ns = when;
        request.decode_started_recorded = true;
    }
}

void ServingRuntimeBase::mark_first_token(size_t request_index, Tick when) {
    auto& request = requests.at(request_index);
    if (!request.first_token_recorded) {
        request.first_token_time_ns = when;
        request.first_token_recorded = true;
    }
}

void ServingRuntimeBase::maybe_mark_first_token_at_decode_start(
    const ServingBatch& batch) {
    if (config.cost_model.first_token_timing !=
        ServingFirstTokenTiming::DecodeStart) {
        return;
    }
    for (const auto& item : batch.items) {
        const auto& request = requests.at(item.request_index);
        if (request.completed_output_tokens == 0) {
            const auto first_token_time =
                batch.scheduled_at_ns +
                cost_model.estimate_first_token_latency_ns(request);
            mark_first_token(item.request_index, first_token_time);
        }
    }
}

void ServingRuntimeBase::mark_request_finished(size_t request_index, Tick when) {
    auto& request = requests.at(request_index);
    request.finish_time_ns = when;
    request.phase = RequestPhase::Finished;
    request.finished_recorded = true;
}

void ServingRuntimeBase::add_prefill_runtime(size_t request_index,
                                             Tick duration) {
    requests.at(request_index).accumulated_prefill_compute_ns += duration;
}

void ServingRuntimeBase::add_decode_runtime(size_t request_index,
                                            Tick duration) {
    requests.at(request_index).accumulated_decode_compute_ns += duration;
}

void ServingRuntimeBase::add_transfer_runtime(size_t request_index,
                                              Tick duration) {
    requests.at(request_index).accumulated_transfer_ns += duration;
}

void ServingRuntimeBase::add_collective_runtime(size_t request_index,
                                                Tick duration) {
    requests.at(request_index).accumulated_collective_ns += duration;
}

void ServingRuntimeBase::add_prefill_breakdown(
    size_t request_index,
    const ServingStageBreakdown& breakdown) {
    auto& request = requests.at(request_index);
    request.accumulated_prefill_breakdown.base_latency_ns +=
        breakdown.base_latency_ns;
    request.accumulated_prefill_breakdown.step_latency_ns +=
        breakdown.step_latency_ns;
    request.accumulated_prefill_breakdown.attention_compute_ns +=
        breakdown.attention_compute_ns;
    request.accumulated_prefill_breakdown.ffn_or_expert_compute_ns +=
        breakdown.ffn_or_expert_compute_ns;
    request.accumulated_prefill_breakdown.tp_collective_ns +=
        breakdown.tp_collective_ns;
    request.accumulated_prefill_breakdown.pp_activation_ns +=
        breakdown.pp_activation_ns;
    request.accumulated_prefill_breakdown.ep_dispatch_ns +=
        breakdown.ep_dispatch_ns;
    request.accumulated_prefill_breakdown.dp_attention_sync_ns +=
        breakdown.dp_attention_sync_ns;
    request.accumulated_prefill_breakdown.pd_kv_transfer_ns +=
        breakdown.pd_kv_transfer_ns;
}

void ServingRuntimeBase::add_decode_breakdown(
    size_t request_index,
    const ServingStageBreakdown& breakdown) {
    auto& request = requests.at(request_index);
    request.accumulated_decode_breakdown.base_latency_ns +=
        breakdown.base_latency_ns;
    request.accumulated_decode_breakdown.step_latency_ns +=
        breakdown.step_latency_ns;
    request.accumulated_decode_breakdown.attention_compute_ns +=
        breakdown.attention_compute_ns;
    request.accumulated_decode_breakdown.ffn_or_expert_compute_ns +=
        breakdown.ffn_or_expert_compute_ns;
    request.accumulated_decode_breakdown.tp_collective_ns +=
        breakdown.tp_collective_ns;
    request.accumulated_decode_breakdown.pp_activation_ns +=
        breakdown.pp_activation_ns;
    request.accumulated_decode_breakdown.ep_dispatch_ns +=
        breakdown.ep_dispatch_ns;
    request.accumulated_decode_breakdown.dp_attention_sync_ns +=
        breakdown.dp_attention_sync_ns;
    request.accumulated_decode_breakdown.pd_kv_transfer_ns +=
        breakdown.pd_kv_transfer_ns;
}

void ServingRuntimeBase::add_transfer_breakdown(
    size_t request_index,
    const ServingStageBreakdown& breakdown) {
    auto& request = requests.at(request_index);
    request.accumulated_transfer_breakdown.base_latency_ns +=
        breakdown.base_latency_ns;
    request.accumulated_transfer_breakdown.step_latency_ns +=
        breakdown.step_latency_ns;
    request.accumulated_transfer_breakdown.attention_compute_ns +=
        breakdown.attention_compute_ns;
    request.accumulated_transfer_breakdown.ffn_or_expert_compute_ns +=
        breakdown.ffn_or_expert_compute_ns;
    request.accumulated_transfer_breakdown.tp_collective_ns +=
        breakdown.tp_collective_ns;
    request.accumulated_transfer_breakdown.pp_activation_ns +=
        breakdown.pp_activation_ns;
    request.accumulated_transfer_breakdown.ep_dispatch_ns +=
        breakdown.ep_dispatch_ns;
    request.accumulated_transfer_breakdown.dp_attention_sync_ns +=
        breakdown.dp_attention_sync_ns;
    request.accumulated_transfer_breakdown.pd_kv_transfer_ns +=
        breakdown.pd_kv_transfer_ns;
}

void ServingRuntimeBase::record_stage_schedule(const ServingBatch& batch,
                                               const std::string& event_name) {
    if (!has_output_path(context.outputs.event_trace_output)) {
        return;
    }
    event_trace_records.push_back(
        ServingEventTraceRecord{Sys::boostedTick(),
                                event_name,
                                batch.batch_id,
                                batch.worker_id,
                                batch.worker_group_id,
                                batch.replica_id,
                                to_string(batch.stage),
                                batch.layout_name,
                                join_request_ids(batch.items, requests),
                                batch.items.size(),
                                total_tokens(batch),
                                batch.duration_ns,
                                batch.include_base_latency,
                                batch.running_request_count_at_schedule,
                                batch.admission_queue_depth_at_schedule,
                                batch.prefill_queue_depth_at_schedule,
                                batch.transfer_queue_depth_at_schedule,
                                batch.decode_queue_depth_at_schedule,
                                batch.inflight_transfer_count_at_schedule});
}

void ServingRuntimeBase::record_stage_metrics(const ServingBatch& batch) {
    if (!has_output_path(context.outputs.stage_metrics_output)) {
        return;
    }
    stage_metrics_records.push_back(ServingStageMetricsRecord{
        batch.batch_id,
        batch.worker_id,
        batch.worker_group_id,
        batch.replica_id,
        to_string(batch.stage),
        batch.layout_name,
        join_request_ids(batch.items, requests),
        batch.items.size(),
        total_tokens(batch),
        batch.include_base_latency,
        batch.scheduled_at_ns,
        batch.scheduled_at_ns + batch.duration_ns,
        batch.duration_ns,
        batch.running_request_count_at_schedule,
        batch.admission_queue_depth_at_schedule,
        batch.prefill_queue_depth_at_schedule,
        batch.transfer_queue_depth_at_schedule,
        batch.decode_queue_depth_at_schedule,
        batch.inflight_transfer_count_at_schedule,
    });
}

void ServingRuntimeBase::record_request_event(size_t request_index,
                                              const std::string& event_name) {
    if (!has_output_path(context.outputs.event_trace_output)) {
        return;
    }
    const auto& request = requests.at(request_index);
    event_trace_records.push_back(
        ServingEventTraceRecord{Sys::boostedTick(),
                                event_name,
                                0,
                                0,
                                0,
                                request.replica_id,
                                to_string(request.phase),
                                "",
                                std::to_string(request.spec.request_id),
                                1,
                                0,
                                0,
                                false,
                                0,
                                0,
                                0,
                                0,
                                0,
                                0});
}

ServingRequestMetrics ServingRuntimeBase::build_request_metrics(
    const ServingRequestState& request) const {
    ServingRequestMetrics metrics;
    metrics.original_index = request.spec.original_index;
    metrics.request_id = request.spec.request_id;
    metrics.arrival_time_ns = request.arrival_time_ns;
    metrics.prompt_tokens = request.spec.prompt_tokens;
    metrics.output_tokens = request.spec.output_tokens;
    metrics.architecture = to_string(config.runtime.architecture);
    metrics.replica_id = request.replica_id;
    metrics.prefill_layout_name =
        request.prefill_group_id.has_value()
            ? topology.group(*request.prefill_group_id).layout.name
            : topology.prefill_layout().name;
    metrics.decode_layout_name =
        request.decode_group_id.has_value()
            ? topology.group(*request.decode_group_id).layout.name
            : topology.decode_layout().name;
    metrics.service_start_ns = request.service_start_ns;
    metrics.prefill_queue_enter_ns = request.prefill_queue_enter_ns;
    metrics.prefill_start_ns = request.prefill_start_ns;
    metrics.prefill_end_time_ns = request.prefill_end_time_ns;
    metrics.transfer_queue_enter_ns = request.transfer_queue_enter_ns;
    metrics.transfer_start_ns = request.transfer_start_ns;
    metrics.transfer_end_ns = request.transfer_end_ns;
    metrics.decode_queue_enter_ns = request.decode_queue_enter_ns;
    metrics.decode_start_ns = request.decode_start_ns;
    metrics.first_token_time_ns = request.first_token_time_ns;
    metrics.finish_time_ns = request.finish_time_ns;
    metrics.queue_delay_ns =
        request.service_started_recorded
            ? request.service_start_ns - request.arrival_time_ns
            : 0;
    metrics.prefill_queue_delay_ns =
        request.prefill_started_recorded
            ? request.prefill_start_ns - request.prefill_queue_enter_ns
            : 0;
    metrics.decode_queue_delay_ns =
        request.decode_started_recorded
            ? request.decode_start_ns - request.decode_queue_enter_ns
            : 0;
    metrics.transfer_queue_delay_ns =
        request.transfer_started_recorded
            ? request.transfer_start_ns - request.transfer_queue_enter_ns
            : 0;
    metrics.prefill_duration_ns =
        request.prefill_started_recorded
            ? request.prefill_end_time_ns - request.prefill_start_ns
            : 0;
    metrics.transfer_duration_ns =
        request.transfer_started_recorded
            ? request.transfer_end_ns - request.transfer_start_ns
            : 0;
    metrics.decode_duration_ns =
        request.decode_started_recorded
            ? request.finish_time_ns - request.decode_start_ns
            : 0;
    metrics.prefill_service_ns = request.accumulated_prefill_compute_ns;
    metrics.transfer_service_ns = request.accumulated_transfer_ns;
    metrics.decode_service_ns = request.accumulated_decode_compute_ns;
    metrics.prefill_stage_wait_ns =
        metrics.prefill_duration_ns > metrics.prefill_service_ns
            ? metrics.prefill_duration_ns - metrics.prefill_service_ns
            : 0;
    metrics.transfer_stage_wait_ns =
        metrics.transfer_duration_ns > metrics.transfer_service_ns
            ? metrics.transfer_duration_ns - metrics.transfer_service_ns
            : 0;
    metrics.decode_stage_wait_ns =
        metrics.decode_duration_ns > metrics.decode_service_ns
            ? metrics.decode_duration_ns - metrics.decode_service_ns
            : 0;
    metrics.total_prefill_queue_wait_ns =
        request.accumulated_prefill_queue_wait_ns;
    metrics.total_transfer_queue_wait_ns =
        request.accumulated_transfer_queue_wait_ns;
    metrics.total_decode_queue_wait_ns =
        request.accumulated_decode_queue_wait_ns;
    metrics.prefill_chunk_count = request.prefill_chunk_count;
    metrics.transfer_handoff_count = request.transfer_handoff_count;
    metrics.max_prefill_chunk_tokens = request.max_prefill_chunk_tokens;
    metrics.max_transfer_chunk_tokens = request.max_transfer_chunk_tokens;
    metrics.ttft_ns = request.first_token_recorded
                          ? request.first_token_time_ns - request.arrival_time_ns
                          : 0;
    metrics.tpot_ns = calculate_serving_tpot_ns(metrics.decode_duration_ns,
                                                request.spec.output_tokens);
    metrics.e2e_ns =
        request.finished_recorded
            ? request.finish_time_ns - request.arrival_time_ns
            : 0;
    metrics.kv_transfer_bytes = request.kv_transfer_bytes;
    metrics.kv_resident_bytes = request.kv_resident_bytes;
    metrics.prefill_breakdown = request.accumulated_prefill_breakdown;
    metrics.decode_breakdown = request.accumulated_decode_breakdown;
    metrics.transfer_breakdown = request.accumulated_transfer_breakdown;
    metrics.goodput = evaluate_serving_goodput(config.slo, metrics.ttft_ns,
                                               metrics.tpot_ns, metrics.e2e_ns);
    return metrics;
}

void ServingRuntimeBase::complete_request(size_t request_index) {
    const auto metrics = build_request_metrics(requests.at(request_index));
    completed_metrics.push_back(metrics);
    emit_serving_request_log(metrics);
    record_request_event(request_index, "request_finished");
    finalize_if_done();
}

void ServingRuntimeBase::finalize_if_done() {
    if (completed_metrics.size() == requests.size()) {
        finalize();
    }
}

void ServingRuntimeBase::finalize() {
    if (finalized) {
        return;
    }
    finalized = true;

    std::vector<double> queue_delay_values;
    std::vector<double> prefill_queue_delay_values;
    std::vector<double> decode_queue_delay_values;
    std::vector<double> transfer_duration_values;
    std::vector<double> ttft_values;
    std::vector<double> tpot_values;
    std::vector<double> e2e_values;
    for (const auto& metrics : completed_metrics) {
        queue_delay_values.push_back(
            static_cast<double>(metrics.queue_delay_ns));
        prefill_queue_delay_values.push_back(
            static_cast<double>(metrics.prefill_queue_delay_ns));
        decode_queue_delay_values.push_back(
            static_cast<double>(metrics.decode_queue_delay_ns));
        transfer_duration_values.push_back(
            static_cast<double>(metrics.transfer_duration_ns));
        ttft_values.push_back(static_cast<double>(metrics.ttft_ns));
        tpot_values.push_back(metrics.tpot_ns);
        e2e_values.push_back(static_cast<double>(metrics.e2e_ns));
    }

    Tick simulation_start_time_ns = 0;
    Tick simulation_end_time_ns = 0;
    uint64_t total_output_tokens = 0;
    if (!completed_metrics.empty()) {
        simulation_start_time_ns = completed_metrics.front().arrival_time_ns;
        simulation_end_time_ns = completed_metrics.front().finish_time_ns;
    }
    for (const auto& metrics : completed_metrics) {
        simulation_start_time_ns =
            std::min(simulation_start_time_ns, metrics.arrival_time_ns);
        simulation_end_time_ns =
            std::max(simulation_end_time_ns, metrics.finish_time_ns);
        total_output_tokens += metrics.output_tokens;
    }
    const auto makespan_ns = simulation_end_time_ns - simulation_start_time_ns;

    emit_serving_summary_log("QUEUE_DELAY",
                             summarize_serving_metric(queue_delay_values));
    emit_serving_summary_log("TTFT", summarize_serving_metric(ttft_values));
    emit_serving_summary_log("TPOT", summarize_serving_metric(tpot_values));
    emit_serving_summary_log("E2E", summarize_serving_metric(e2e_values));
    emit_serving_throughput_log(completed_metrics.size(), makespan_ns,
                                total_output_tokens);

    write_serving_metrics_csv(context.outputs.request_metrics_output, config,
                              completed_metrics);
    write_serving_summary_json(context.outputs.request_summary_output, config,
                               completed_metrics);
    write_serving_run_metadata_json(
        context.outputs.request_run_metadata_output, config, context.outputs,
        context.binary_name);
    write_serving_event_trace_csv(context.outputs.event_trace_output, config,
                                  event_trace_records);
    write_serving_stage_metrics_csv(context.outputs.stage_metrics_output, config,
                                    stage_metrics_records);
}

DataSet* ServingRuntimeBase::issue_collective(
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

std::unique_ptr<ServingRuntime> create_serving_runtime(
    ServingRuntimeContext context) {
    switch (context.config->runtime.architecture) {
    case ServingArchitecture::SerialBaseline:
        return std::make_unique<SerialServingRuntime>(std::move(context));
    case ServingArchitecture::Colocated:
    case ServingArchitecture::ColocatedChunked:
        return std::make_unique<ColocatedServingRuntime>(std::move(context));
    case ServingArchitecture::PdDisaggregated:
        return std::make_unique<PdServingRuntime>(std::move(context));
    }
    serving_runtime_error("Unknown serving runtime architecture");
}

}  // namespace AstraSim
