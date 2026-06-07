#include "astra-sim/workload/SerialServingRuntime.hh"

#include <algorithm>
#include <stdexcept>

#include "astra-sim/common/Logging.hh"
#include "astra-sim/system/IntData.hh"

namespace AstraSim {

namespace {

[[noreturn]] void serial_runtime_error(const std::string& message) {
    LoggerFactory::get_logger("serving")->critical(message);
    throw std::runtime_error(message);
}

}  // namespace

SerialServingRuntime::SerialServingRuntime(ServingRuntimeContext context)
    : ServingRuntimeBase(std::move(context)),
      request_active(false),
      active_request_index(0),
      pending_collective_completions(0),
      pending_collective_continuation(CollectiveContinuation::None),
      pending_collective_request_index(0),
      pending_collective_token_index(0) {}

void SerialServingRuntime::fire() {
    for (const auto request_index : arrival_order) {
        if (requests[request_index].spec.arrival_time_ns == 0) {
            enqueue_request(request_index);
            continue;
        }
        control_sys->register_event(
            event_sink, EventType::ServingRequestArrival,
            new ServingRuntimeEventData(ServingStageType::RequestArrival,
                                        request_index),
            requests[request_index].spec.arrival_time_ns);
    }
    try_start_next_request();
    if (requests.empty()) {
        finalize();
    }
}

void SerialServingRuntime::call(EventType event, CallData* data) {
    if (event == EventType::ServingRequestArrival ||
        event == EventType::ServingStageCompleted) {
        auto* serving_data = static_cast<ServingRuntimeEventData*>(data);
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
        (void)static_cast<IntData*>(data);
        pending_collective_completions--;

        if (pending_collective_completions == 0) {
            const auto continuation = pending_collective_continuation;
            const auto request_index = pending_collective_request_index;
            const auto token_index = pending_collective_token_index;
            pending_collective_continuation = CollectiveContinuation::None;
            pending_collective_request_index = 0;
            pending_collective_token_index = 0;

            if (continuation == CollectiveContinuation::AfterPrefill) {
                mark_prefill_end(request_index, Sys::boostedTick());
                requests[request_index].phase = RequestPhase::DecodeQueued;
                mark_decode_queue_enter(request_index, Sys::boostedTick());
                start_decode_compute(request_index, 1);
                return;
            }
            if (continuation == CollectiveContinuation::AfterDecode) {
                complete_decode_token(request_index, token_index);
                return;
            }
            serial_runtime_error("Serving collective completed without a "
                                 "registered continuation");
        }
        return;
    }

    serial_runtime_error("Unsupported serving coordinator event");
}

void SerialServingRuntime::enqueue_request(size_t request_index) {
    mark_prefill_queue_enter(request_index, requests[request_index].arrival_time_ns);
    requests[request_index].phase = RequestPhase::WaitingForAdmission;
    pending_requests.push_back(request_index);
    record_request_event(request_index, "request_arrived");
}

void SerialServingRuntime::try_start_next_request() {
    if (request_active || pending_requests.empty()) {
        return;
    }

    request_active = true;
    active_request_index = pending_requests.front();
    pending_requests.pop_front();

    mark_service_start(active_request_index);
    mark_prefill_start(active_request_index, Sys::boostedTick());
    requests[active_request_index].phase = RequestPhase::PrefillRunning;
    start_prefill_compute(active_request_index);
}

void SerialServingRuntime::start_prefill_compute(size_t request_index) {
    const auto delay = cost_model.estimate_serial_prefill_ns(
        requests[request_index].spec.prompt_tokens);
    if (delay == 0) {
        handle_stage_completion(request_index, ServingStageType::SerialPrefill,
                                0);
        return;
    }

    control_sys->register_event(
        event_sink, EventType::ServingStageCompleted,
        new ServingRuntimeEventData(ServingStageType::SerialPrefill,
                                    request_index, 0, 0, 0),
        delay);
}

void SerialServingRuntime::start_decode_compute(size_t request_index,
                                                uint64_t token_index) {
    const auto decode_start = Sys::boostedTick();
    mark_decode_start(request_index, decode_start);
    if (token_index == 1 &&
        config.cost_model.first_token_timing ==
            ServingFirstTokenTiming::DecodeStart) {
        mark_first_token(request_index,
                         decode_start + config.cost_model.first_token_latency_ns);
    }
    requests[request_index].phase = RequestPhase::DecodeRunning;
    const auto delay =
        cost_model.estimate_serial_decode_ns(token_index == 1, 1);
    if (delay == 0) {
        handle_stage_completion(request_index, ServingStageType::SerialDecode,
                                token_index);
        return;
    }

    control_sys->register_event(
        event_sink, EventType::ServingStageCompleted,
        new ServingRuntimeEventData(ServingStageType::SerialDecode,
                                    request_index, 0, 0, token_index),
        delay);
}

void SerialServingRuntime::handle_stage_completion(size_t request_index,
                                                   ServingStageType stage,
                                                   uint64_t token_index) {
    if (stage == ServingStageType::SerialPrefill) {
        const auto breakdown = cost_model.estimate_serial_prefill_breakdown_ns(
            requests[request_index].spec.prompt_tokens);
        add_prefill_runtime(request_index, breakdown.total_ns());
        add_prefill_breakdown(request_index, breakdown);
        const auto scaled_collective_size =
            cost_model.scale_collective_size(config.prefill_collective.size_bytes);
        if (config.prefill_collective.enabled && scaled_collective_size > 0) {
            start_collective(config.prefill_collective,
                             CollectiveContinuation::AfterPrefill,
                             request_index, 0);
        } else {
            requests[request_index].kv_resident_bytes =
                cost_model.estimate_kv_transfer_bytes(requests[request_index]) /
                std::max<uint64_t>(1, topology.colocated_layout().dp_attention_degree);
            mark_prefill_end(request_index, Sys::boostedTick());
            requests[request_index].phase = RequestPhase::DecodeQueued;
            mark_decode_queue_enter(request_index, Sys::boostedTick());
            start_decode_compute(request_index, 1);
        }
        return;
    }

    if (stage == ServingStageType::SerialDecode) {
        const auto breakdown = cost_model.estimate_serial_decode_breakdown_ns(
            token_index == 1, 1);
        add_decode_runtime(request_index, breakdown.total_ns());
        add_decode_breakdown(request_index, breakdown);
        const auto scaled_collective_size =
            cost_model.scale_collective_size(config.decode_collective.size_bytes);
        if (config.decode_collective.enabled && scaled_collective_size > 0) {
            start_collective(config.decode_collective,
                             CollectiveContinuation::AfterDecode,
                             request_index, token_index);
        } else {
            complete_decode_token(request_index, token_index);
        }
        return;
    }

    serial_runtime_error("Unknown serving stage completion event");
}

void SerialServingRuntime::start_collective(
    const ServingCollectiveSpec& collective,
    CollectiveContinuation continuation,
    size_t request_index,
    uint64_t token_index) {
    const auto scaled_collective_size =
        cost_model.scale_collective_size(collective.size_bytes);
    if (scaled_collective_size == 0) {
        if (continuation == CollectiveContinuation::AfterPrefill) {
            mark_prefill_end(request_index, Sys::boostedTick());
            requests[request_index].phase = RequestPhase::DecodeQueued;
            mark_decode_queue_enter(request_index, Sys::boostedTick());
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
        dataset->set_notifier(event_sink, EventType::ServingCollectiveCompleted);
    }
}

void SerialServingRuntime::complete_decode_token(size_t request_index,
                                                 uint64_t token_index) {
    auto& request = requests[request_index];
    const auto now = Sys::boostedTick();

    request.completed_output_tokens = token_index;
    if (token_index == 1) {
        mark_first_token(request_index, now);
    }

    if (token_index >= request.spec.output_tokens) {
        mark_request_finished(request_index, now);
        request_active = false;
        complete_request(request_index);
        if (!finalized) {
            try_start_next_request();
        }
        return;
    }

    requests[request_index].phase = RequestPhase::DecodeQueued;
    start_decode_compute(request_index, token_index + 1);
}

}  // namespace AstraSim
