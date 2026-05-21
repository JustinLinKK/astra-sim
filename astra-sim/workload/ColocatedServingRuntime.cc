#include "astra-sim/workload/ColocatedServingRuntime.hh"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "astra-sim/common/Logging.hh"
#include "astra-sim/workload/serving/topology/ServingBatchBuilder.hh"

namespace AstraSim {

namespace {

[[noreturn]] void colocated_runtime_error(const std::string& message) {
    LoggerFactory::get_logger("serving")->critical(message);
    throw std::runtime_error(message);
}

}  // namespace

ColocatedServingRuntime::ColocatedServingRuntime(ServingRuntimeContext context)
    : ServingRuntimeBase(std::move(context)),
      waiting_requests_by_replica(topology.replica_count()),
      running_requests_by_replica(topology.replica_count()),
      chunking_enabled(config.runtime.architecture ==
                       ServingArchitecture::ColocatedChunked) {
    for (const auto group_id : topology.group_ids_for_role(
             ServingWorkerGroupRole::Colocated)) {
        balanced_decode_budget_remaining.emplace(group_id, 4);
    }
}

size_t ColocatedServingRuntime::max_running_requests_per_replica() const {
    return static_cast<size_t>(std::max<uint64_t>(
        1,
        static_cast<uint64_t>(std::ceil(
            static_cast<long double>(config.scheduler.max_running_requests) /
            static_cast<long double>(std::max<size_t>(1, topology.replica_count()))))));
}

size_t ColocatedServingRuntime::running_request_count(size_t replica_id) const {
    size_t count = 0;
    for (const auto request_index : running_requests_by_replica.at(replica_id)) {
        if (!requests[request_index].finished_recorded) {
            count++;
        }
    }
    return count;
}

size_t ColocatedServingRuntime::prefill_queue_depth(size_t replica_id) const {
    size_t count = 0;
    for (const auto request_index : running_requests_by_replica.at(replica_id)) {
        const auto& request = requests[request_index];
        if (!request.finished_recorded && !request.in_active_batch &&
            request.remaining_prefill_tokens > 0) {
            count++;
        }
    }
    return count;
}

size_t ColocatedServingRuntime::decode_queue_depth(size_t replica_id) const {
    size_t count = 0;
    for (const auto request_index : running_requests_by_replica.at(replica_id)) {
        const auto& request = requests[request_index];
        if (!request.finished_recorded && !request.in_active_batch &&
            request.remaining_prefill_tokens == 0 &&
            request.completed_output_tokens < request.spec.output_tokens) {
            count++;
        }
    }
    return count;
}

void ColocatedServingRuntime::annotate_batch_snapshot(ServingBatch* batch) const {
    batch->running_request_count_at_schedule =
        running_request_count(batch->replica_id);
    batch->admission_queue_depth_at_schedule =
        waiting_requests_by_replica.at(batch->replica_id).size();
    batch->prefill_queue_depth_at_schedule =
        prefill_queue_depth(batch->replica_id);
    batch->decode_queue_depth_at_schedule =
        decode_queue_depth(batch->replica_id);
}

void ColocatedServingRuntime::fire() {
    for (const auto request_index : arrival_order) {
        if (requests[request_index].spec.arrival_time_ns == 0) {
            requests[request_index].phase = RequestPhase::WaitingForAdmission;
            mark_prefill_queue_enter(
                request_index, requests[request_index].arrival_time_ns);
            waiting_requests_by_replica[requests[request_index].replica_id]
                .push_back(request_index);
            record_request_event(request_index, "request_arrived");
            continue;
        }
        control_sys->register_event(
            event_sink, EventType::ServingRequestArrival,
            new ServingRuntimeEventData(ServingStageType::RequestArrival,
                                        request_index),
            requests[request_index].spec.arrival_time_ns);
    }
    try_schedule();
    if (requests.empty()) {
        finalize();
    }
}

void ColocatedServingRuntime::call(EventType event, CallData* data) {
    if (event == EventType::ServingRequestArrival) {
        auto* serving_data = static_cast<ServingRuntimeEventData*>(data);
        const auto request_index = serving_data->request_index;
        delete serving_data;
        handle_request_arrival(request_index);
        return;
    }
    if (event == EventType::ServingStageCompleted) {
        auto* serving_data = static_cast<ServingRuntimeEventData*>(data);
        const auto batch_id = serving_data->batch_id;
        delete serving_data;
        complete_batch(batch_id);
        return;
    }

    colocated_runtime_error("Unsupported colocated serving event");
}

void ColocatedServingRuntime::handle_request_arrival(size_t request_index) {
    requests[request_index].phase = RequestPhase::WaitingForAdmission;
    mark_prefill_queue_enter(request_index, requests[request_index].arrival_time_ns);
    waiting_requests_by_replica[requests[request_index].replica_id].push_back(
        request_index);
    record_request_event(request_index, "request_arrived");
    try_schedule();
}

void ColocatedServingRuntime::admit_waiting_requests(size_t replica_id) {
    auto& waiting = waiting_requests_by_replica.at(replica_id);
    auto& running = running_requests_by_replica.at(replica_id);
    const auto running_limit = max_running_requests_per_replica();
    while (!waiting.empty() && running.size() < running_limit) {
        const auto request_index = waiting.front();
        waiting.pop_front();
        running.push_back(request_index);
        requests[request_index].phase = RequestPhase::PrefillQueued;
    }
}

bool ColocatedServingRuntime::has_prefill_work(size_t replica_id) const {
    for (const auto request_index : running_requests_by_replica.at(replica_id)) {
        const auto& request = requests[request_index];
        if (!request.finished_recorded && !request.in_active_batch &&
            request.remaining_prefill_tokens > 0) {
            return true;
        }
    }
    return false;
}

bool ColocatedServingRuntime::has_decode_work(size_t replica_id) const {
    for (const auto request_index : running_requests_by_replica.at(replica_id)) {
        const auto& request = requests[request_index];
        if (!request.finished_recorded && !request.in_active_batch &&
            request.remaining_prefill_tokens == 0 &&
            request.completed_output_tokens < request.spec.output_tokens) {
            return true;
        }
    }
    return false;
}

void ColocatedServingRuntime::reset_balanced_budget(size_t group_id) {
    balanced_decode_budget_remaining[group_id] = 4;
}

std::optional<ServingBatch> ColocatedServingRuntime::build_decode_batch(
    size_t group_id) {
    const auto& group = topology.group(group_id);
    auto batch = ServingBatchBuilder::build_decode_from_running(
        next_batch_id++, ServingStageType::ColocatedDecode, group_id,
        group.replica_id, group.layout,
        running_requests_by_replica.at(group.replica_id), requests,
        config.scheduler.max_decode_batch_requests);
    if (!batch.has_value()) {
        return std::nullopt;
    }
    batch->breakdown = cost_model.estimate_decode_breakdown(*batch);
    batch->duration_ns = batch->breakdown.total_ns() +
                         cost_model.estimate_collective_ns(
                             config.decode_collective);
    return batch;
}

std::optional<ServingBatch> ColocatedServingRuntime::build_prefill_batch(
    size_t group_id) {
    const auto& group = topology.group(group_id);
    auto batch = ServingBatchBuilder::build_prefill_from_running(
        next_batch_id++, ServingStageType::ColocatedPrefill, group_id,
        group.replica_id, group.layout,
        running_requests_by_replica.at(group.replica_id), requests,
        config.scheduler, chunking_enabled);
    if (!batch.has_value()) {
        return std::nullopt;
    }
    batch->breakdown = cost_model.estimate_prefill_breakdown(*batch);
    batch->duration_ns = batch->breakdown.total_ns() +
                         cost_model.estimate_collective_ns(
                             config.prefill_collective);
    return batch;
}

std::optional<ServingBatch> ColocatedServingRuntime::build_next_batch(
    size_t group_id) {
    const auto replica_id = topology.group(group_id).replica_id;
    const bool decode_available = has_decode_work(replica_id);
    const bool prefill_available = has_prefill_work(replica_id);
    if (!decode_available && !prefill_available) {
        return std::nullopt;
    }

    auto build_with_preference =
        [&](bool prefer_decode) -> std::optional<ServingBatch> {
        if (prefer_decode) {
            if (auto batch = build_decode_batch(group_id); batch.has_value()) {
                return batch;
            }
            return build_prefill_batch(group_id);
        }
        if (auto batch = build_prefill_batch(group_id); batch.has_value()) {
            return batch;
        }
        return build_decode_batch(group_id);
    };

    switch (config.scheduler.scheduler_policy) {
    case ServingSchedulerPolicy::Serial:
    case ServingSchedulerPolicy::PrefillFirst:
        return build_with_preference(false);
    case ServingSchedulerPolicy::DecodeFirst:
        return build_with_preference(true);
    case ServingSchedulerPolicy::Balanced:
        if (decode_available && prefill_available) {
            if (balanced_decode_budget_remaining[group_id] > 0) {
                balanced_decode_budget_remaining[group_id]--;
                return build_with_preference(true);
            }
            reset_balanced_budget(group_id);
            return build_with_preference(false);
        }
        reset_balanced_budget(group_id);
        return build_with_preference(decode_available);
    }
    return std::nullopt;
}

void ColocatedServingRuntime::schedule_batch(ServingBatch batch) {
    annotate_batch_snapshot(&batch);
    topology.mark_group_busy(batch.worker_group_id, batch.batch_id);
    for (const auto& item : batch.items) {
        auto& request = requests[item.request_index];
        request.in_active_batch = true;
        if (batch.stage == ServingStageType::ColocatedPrefill) {
            request.prefill_group_id = batch.worker_group_id;
            mark_service_start(item.request_index);
            mark_prefill_start(item.request_index, batch.scheduled_at_ns);
            request.phase = RequestPhase::PrefillRunning;
        } else {
            request.decode_group_id = batch.worker_group_id;
            mark_decode_start(item.request_index, batch.scheduled_at_ns);
            request.phase = RequestPhase::DecodeRunning;
        }
    }
    record_stage_schedule(batch, "batch_scheduled");
    active_batches.emplace(batch.batch_id, batch);

    if (batch.duration_ns == 0) {
        complete_batch(batch.batch_id);
        return;
    }

    control_sys->register_event(
        event_sink, EventType::ServingStageCompleted,
        new ServingRuntimeEventData(batch.stage, batch.items.front().request_index,
                                    batch.batch_id, batch.worker_group_id),
        batch.duration_ns);
}

void ColocatedServingRuntime::complete_prefill_batch(const ServingBatch& batch) {
    const auto now = Sys::boostedTick();
    for (const auto& item : batch.items) {
        auto& request = requests[item.request_index];
        request.in_active_batch = false;
        add_prefill_runtime(item.request_index, batch.duration_ns);
        add_prefill_breakdown(item.request_index, batch.breakdown);
        request.prefill_chunk_count++;
        request.max_prefill_chunk_tokens =
            std::max<uint64_t>(request.max_prefill_chunk_tokens, item.tokens);
        request.completed_prefill_tokens += item.tokens;
        request.remaining_prefill_tokens -= item.tokens;
        if (request.remaining_prefill_tokens == 0) {
            request.kv_resident_bytes =
                cost_model.estimate_kv_transfer_bytes(request) /
                std::max<uint64_t>(1, batch.layout.dp_attention_degree);
            mark_prefill_end(item.request_index, now);
            request.phase = RequestPhase::DecodeQueued;
            mark_decode_queue_enter(item.request_index, now);
        } else {
            request.phase = RequestPhase::PrefillQueued;
            mark_prefill_queue_enter(item.request_index, now);
        }
    }
}

void ColocatedServingRuntime::complete_decode_batch(const ServingBatch& batch) {
    const auto now = Sys::boostedTick();
    std::vector<size_t> finished_requests;
    for (const auto& item : batch.items) {
        auto& request = requests[item.request_index];
        request.in_active_batch = false;
        add_decode_runtime(item.request_index, batch.duration_ns);
        add_decode_breakdown(item.request_index, batch.breakdown);
        request.completed_output_tokens += item.tokens;
        if (request.completed_output_tokens == 1) {
            mark_first_token(item.request_index, now);
        }
        if (request.completed_output_tokens >= request.spec.output_tokens) {
            mark_request_finished(item.request_index, now);
            finished_requests.push_back(item.request_index);
        } else {
            request.phase = RequestPhase::DecodeQueued;
            mark_decode_queue_enter(item.request_index, now);
        }
    }

    if (!finished_requests.empty()) {
        auto& running = running_requests_by_replica.at(batch.replica_id);
        running.erase(std::remove_if(running.begin(), running.end(),
                                     [&](size_t request_index) {
                                         return std::find(
                                                    finished_requests.begin(),
                                                    finished_requests.end(),
                                                    request_index) !=
                                                finished_requests.end();
                                     }),
                      running.end());
        for (const auto request_index : finished_requests) {
            complete_request(request_index);
        }
    }
}

void ColocatedServingRuntime::complete_batch(uint64_t batch_id) {
    const auto batch_iter = active_batches.find(batch_id);
    if (batch_iter == active_batches.end()) {
        colocated_runtime_error("Unknown colocated batch completion");
    }
    const auto batch = batch_iter->second;
    active_batches.erase(batch_iter);
    topology.mark_group_idle(batch.worker_group_id);
    record_stage_schedule(batch, "batch_completed");
    record_stage_metrics(batch);

    switch (batch.stage) {
    case ServingStageType::ColocatedPrefill:
        complete_prefill_batch(batch);
        break;
    case ServingStageType::ColocatedDecode:
        complete_decode_batch(batch);
        break;
    default:
        colocated_runtime_error("Unsupported colocated batch stage");
    }

    if (!finalized) {
        try_schedule();
    }
}

void ColocatedServingRuntime::try_schedule() {
    for (size_t replica_id = 0; replica_id < topology.replica_count();
         ++replica_id) {
        admit_waiting_requests(replica_id);
    }

    while (true) {
        bool scheduled_any = false;
        for (const auto group_id :
             topology.group_ids_for_role(ServingWorkerGroupRole::Colocated)) {
            if (topology.group(group_id).busy) {
                continue;
            }
            auto batch = build_next_batch(group_id);
            if (!batch.has_value()) {
                continue;
            }
            schedule_batch(*batch);
            scheduled_any = true;
        }
        if (!scheduled_any) {
            return;
        }
    }
}

}  // namespace AstraSim
