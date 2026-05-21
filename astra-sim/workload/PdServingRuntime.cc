#include "astra-sim/workload/PdServingRuntime.hh"

#include <algorithm>
#include <stdexcept>

#include "astra-sim/common/Logging.hh"
#include "astra-sim/workload/serving/topology/ServingBatchBuilder.hh"

namespace AstraSim {

namespace {

[[noreturn]] void pd_runtime_error(const std::string& message) {
    LoggerFactory::get_logger("serving")->critical(message);
    throw std::runtime_error(message);
}

}  // namespace

PdServingRuntime::PdServingRuntime(ServingRuntimeContext context)
    : ServingRuntimeBase(std::move(context)),
      prefill_queues(topology.replica_count()),
      decode_queues(topology.replica_count()),
      chunking_enabled(config.scheduler.chunked_prefill_size > 0) {}

size_t PdServingRuntime::running_request_count(size_t replica_id) const {
    size_t count = 0;
    for (const auto& request : requests) {
        if (request.replica_id == replica_id &&
            request.phase != RequestPhase::Arrived &&
            !request.finished_recorded) {
            count++;
        }
    }
    return count;
}

size_t PdServingRuntime::inflight_transfer_count() const {
    size_t count = 0;
    for (const auto& entry : active_batches) {
        if (entry.second.stage == ServingStageType::PdTransfer) {
            count++;
        }
    }
    return count;
}

void PdServingRuntime::annotate_batch_snapshot(ServingBatch* batch) const {
    batch->running_request_count_at_schedule =
        running_request_count(batch->replica_id);
    batch->prefill_queue_depth_at_schedule =
        prefill_queues.at(batch->replica_id).size();
    batch->transfer_queue_depth_at_schedule = transfer_queue.size();
    batch->decode_queue_depth_at_schedule =
        decode_queues.at(batch->replica_id).size();
    if (batch->stage == ServingStageType::PdPrefill) {
        batch->prefill_queue_depth_at_schedule += batch->items.size();
    } else if (batch->stage == ServingStageType::PdTransfer) {
        batch->transfer_queue_depth_at_schedule += batch->items.size();
    } else if (batch->stage == ServingStageType::PdDecode) {
        batch->decode_queue_depth_at_schedule += batch->items.size();
    }
    batch->inflight_transfer_count_at_schedule = inflight_transfer_count();
}

void PdServingRuntime::fire() {
    for (const auto request_index : arrival_order) {
        if (requests[request_index].spec.arrival_time_ns == 0) {
            requests[request_index].phase = RequestPhase::PrefillQueued;
            mark_prefill_queue_enter(
                request_index, requests[request_index].arrival_time_ns);
            prefill_queues[requests[request_index].replica_id].push_back(
                request_index);
            record_request_event(request_index, "request_arrived");
            continue;
        }
        control_sys->register_event(
            event_sink, EventType::ServingRequestArrival,
            new ServingRuntimeEventData(ServingStageType::RequestArrival,
                                        request_index),
            requests[request_index].spec.arrival_time_ns);
    }
    try_schedule_prefill();
    if (requests.empty()) {
        finalize();
    }
}

void PdServingRuntime::call(EventType event, CallData* data) {
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
        const auto stage = serving_data->stage;
        delete serving_data;
        if (stage == ServingStageType::PdTransfer) {
            complete_transfer(batch_id);
            return;
        }
        complete_batch(batch_id);
        return;
    }

    pd_runtime_error("Unsupported PD serving event");
}

void PdServingRuntime::handle_request_arrival(size_t request_index) {
    requests[request_index].phase = RequestPhase::PrefillQueued;
    mark_prefill_queue_enter(request_index, requests[request_index].arrival_time_ns);
    prefill_queues[requests[request_index].replica_id].push_back(request_index);
    record_request_event(request_index, "request_arrived");
    try_schedule_prefill();
}

std::optional<ServingBatch> PdServingRuntime::build_prefill_batch(
    size_t group_id) {
    const auto& group = topology.group(group_id);
    auto batch = ServingBatchBuilder::build_prefill_from_queue(
        next_batch_id++, ServingStageType::PdPrefill, group_id, group.replica_id,
        group.layout, prefill_queues.at(group.replica_id), requests, *config.pd,
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

std::optional<ServingBatch> PdServingRuntime::build_decode_batch(
    size_t group_id) {
    const auto& group = topology.group(group_id);
    auto batch = ServingBatchBuilder::build_decode_from_queue(
        next_batch_id++, ServingStageType::PdDecode, group_id, group.replica_id,
        group.layout, decode_queues.at(group.replica_id), requests,
        config.pd->decode_max_batch_requests);
    if (!batch.has_value()) {
        return std::nullopt;
    }
    batch->breakdown = cost_model.estimate_decode_breakdown(*batch);
    batch->duration_ns = batch->breakdown.total_ns() +
                         cost_model.estimate_collective_ns(
                             config.decode_collective);
    return batch;
}

void PdServingRuntime::schedule_batch(ServingBatch batch) {
    annotate_batch_snapshot(&batch);
    topology.mark_group_busy(batch.worker_group_id, batch.batch_id);
    for (const auto& item : batch.items) {
        auto& request = requests[item.request_index];
        request.in_active_batch = true;
        if (batch.stage == ServingStageType::PdPrefill) {
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

void PdServingRuntime::schedule_transfer(size_t request_index) {
    ServingBatch batch;
    batch.batch_id = next_batch_id++;
    batch.stage = ServingStageType::PdTransfer;
    batch.worker_id = 0;
    batch.worker_group_id = 0;
    batch.replica_id = requests[request_index].replica_id;
    batch.scheduled_at_ns = Sys::boostedTick();
    batch.layout = topology.decode_layout();
    batch.layout_name = batch.layout.name.empty() ? "decode" : batch.layout.name;
    batch.items.push_back(
        ServingBatchItem{request_index, requests[request_index].spec.prompt_tokens});
    batch.breakdown = cost_model.estimate_transfer_breakdown(requests[request_index]);
    batch.duration_ns = batch.breakdown.total_ns();
    annotate_batch_snapshot(&batch);

    requests[request_index].in_active_batch = true;
    requests[request_index].phase = RequestPhase::TransferRunning;
    mark_transfer_start(request_index, batch.scheduled_at_ns);
    record_stage_schedule(batch, "batch_scheduled");
    active_batches.emplace(batch.batch_id, batch);

    if (batch.duration_ns == 0) {
        complete_transfer(batch.batch_id);
        return;
    }

    control_sys->register_event(
        event_sink, EventType::ServingStageCompleted,
        new ServingRuntimeEventData(batch.stage, request_index, batch.batch_id, 0),
        batch.duration_ns);
}

void PdServingRuntime::complete_prefill_batch(const ServingBatch& batch) {
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
        if (request.remaining_prefill_tokens > 0) {
            request.phase = RequestPhase::PrefillQueued;
            mark_prefill_queue_enter(item.request_index, now);
            prefill_queues[request.replica_id].push_back(item.request_index);
            continue;
        }
        mark_prefill_end(item.request_index, now);
        if (config.pd->transfer.enabled) {
            request.phase = RequestPhase::TransferQueued;
            mark_transfer_queue_enter(item.request_index, now);
            transfer_queue.push_back(item.request_index);
        } else {
            request.kv_resident_bytes =
                cost_model.estimate_kv_transfer_bytes(request) /
                std::max<uint64_t>(1, batch.layout.dp_attention_degree);
            request.phase = RequestPhase::DecodeQueued;
            mark_decode_queue_enter(item.request_index, now);
            decode_queues[request.replica_id].push_back(item.request_index);
        }
    }
}

void PdServingRuntime::complete_decode_batch(const ServingBatch& batch) {
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
            decode_queues[request.replica_id].push_back(item.request_index);
        }
    }
    for (const auto request_index : finished_requests) {
        complete_request(request_index);
    }
}

void PdServingRuntime::complete_transfer(uint64_t batch_id) {
    const auto batch_iter = active_batches.find(batch_id);
    if (batch_iter == active_batches.end()) {
        pd_runtime_error("Unknown PD transfer completion");
    }
    const auto batch = batch_iter->second;
    active_batches.erase(batch_iter);
    const auto request_index = batch.items.front().request_index;
    requests[request_index].in_active_batch = false;
    add_transfer_runtime(request_index, batch.duration_ns);
    add_transfer_breakdown(request_index, batch.breakdown);
    requests[request_index].transfer_handoff_count++;
    requests[request_index].max_transfer_chunk_tokens =
        std::max<uint64_t>(requests[request_index].max_transfer_chunk_tokens,
                           batch.items.front().tokens);
    requests[request_index].kv_transfer_bytes =
        cost_model.estimate_kv_transfer_bytes(requests[request_index]);
    requests[request_index].kv_resident_bytes =
        requests[request_index].kv_transfer_bytes /
        std::max<uint64_t>(1, topology.decode_layout().dp_attention_degree);
    mark_transfer_end(request_index, Sys::boostedTick());
    requests[request_index].phase = RequestPhase::DecodeQueued;
    mark_decode_queue_enter(request_index, Sys::boostedTick());
    decode_queues[requests[request_index].replica_id].push_back(request_index);
    record_stage_schedule(batch, "batch_completed");
    record_stage_metrics(batch);

    if (!finalized) {
        try_schedule_decode();
    }
}

void PdServingRuntime::complete_batch(uint64_t batch_id) {
    const auto batch_iter = active_batches.find(batch_id);
    if (batch_iter == active_batches.end()) {
        pd_runtime_error("Unknown PD batch completion");
    }
    const auto batch = batch_iter->second;
    active_batches.erase(batch_iter);
    record_stage_schedule(batch, "batch_completed");
    record_stage_metrics(batch);

    topology.mark_group_idle(batch.worker_group_id);

    switch (batch.stage) {
    case ServingStageType::PdPrefill:
        complete_prefill_batch(batch);
        break;
    case ServingStageType::PdDecode:
        complete_decode_batch(batch);
        break;
    default:
        pd_runtime_error("Unsupported PD stage completion");
    }

    if (!finalized) {
        try_schedule_prefill();
        try_schedule_transfer();
        try_schedule_decode();
    }
}

void PdServingRuntime::try_schedule_prefill() {
    while (true) {
        bool scheduled_any = false;
        for (const auto group_id :
             topology.group_ids_for_role(ServingWorkerGroupRole::Prefill)) {
            if (topology.group(group_id).busy) {
                continue;
            }
            auto batch = build_prefill_batch(group_id);
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

void PdServingRuntime::try_schedule_transfer() {
    while (!transfer_queue.empty()) {
        const auto request_index = transfer_queue.front();
        transfer_queue.pop_front();
        if (requests[request_index].in_active_batch ||
            requests[request_index].finished_recorded) {
            continue;
        }
        schedule_transfer(request_index);
    }
}

void PdServingRuntime::try_schedule_decode() {
    while (true) {
        bool scheduled_any = false;
        for (const auto group_id :
             topology.group_ids_for_role(ServingWorkerGroupRole::Decode)) {
            if (topology.group(group_id).busy) {
                continue;
            }
            auto batch = build_decode_batch(group_id);
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
