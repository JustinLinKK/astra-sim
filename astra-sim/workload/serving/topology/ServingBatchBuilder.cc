#include "astra-sim/workload/serving/topology/ServingBatchBuilder.hh"

#include <algorithm>
#include <limits>

#include "astra-sim/system/Sys.hh"
#include "astra-sim/workload/ServingConfig.hh"
#include "astra-sim/workload/ServingRuntime.hh"

namespace AstraSim {

namespace {

uint64_t prefill_token_limit(const ServingSchedulerConfig& scheduler) {
    return scheduler.max_prefill_batch_tokens == 0
               ? std::numeric_limits<uint64_t>::max()
               : scheduler.max_prefill_batch_tokens;
}

uint64_t pd_prefill_token_limit(const ServingPdConfig& pd) {
    return pd.prefill_max_batch_tokens == 0
               ? std::numeric_limits<uint64_t>::max()
               : pd.prefill_max_batch_tokens;
}

ServingBatch make_base_batch(uint64_t batch_id,
                             ServingStageType stage,
                             size_t group_id,
                             size_t replica_id,
                             const ParallelismLayoutSpec& layout) {
    ServingBatch batch;
    batch.batch_id = batch_id;
    batch.stage = stage;
    batch.worker_id = group_id;
    batch.worker_group_id = group_id;
    batch.replica_id = replica_id;
    batch.layout = layout;
    batch.layout_name = layout.name.empty() ? "default" : layout.name;
    batch.scheduled_at_ns = Sys::boostedTick();
    return batch;
}

}  // namespace

std::optional<ServingBatch> ServingBatchBuilder::build_prefill_from_running(
    uint64_t batch_id,
    ServingStageType stage,
    size_t group_id,
    size_t replica_id,
    const ParallelismLayoutSpec& layout,
    const std::vector<size_t>& running_requests,
    const std::vector<ServingRequestState>& requests,
    const ServingSchedulerConfig& scheduler,
    bool chunking_enabled) {
    auto batch = make_base_batch(batch_id, stage, group_id, replica_id, layout);
    const auto request_limit = scheduler.prefill_max_requests;
    const auto token_limit = prefill_token_limit(scheduler);
    uint64_t total_tokens = 0;

    for (const auto request_index : running_requests) {
        const auto& request = requests[request_index];
        if (request.replica_id != replica_id || request.finished_recorded ||
            request.in_active_batch || request.remaining_prefill_tokens == 0) {
            continue;
        }
        const auto request_tokens =
            chunking_enabled
                ? std::min<uint64_t>(request.remaining_prefill_tokens,
                                     scheduler.chunked_prefill_size)
                : request.remaining_prefill_tokens;
        const bool fits =
            batch.items.empty() || total_tokens + request_tokens <= token_limit;
        if (!fits) {
            continue;
        }
        batch.items.push_back(ServingBatchItem{request_index, request_tokens});
        batch.sequence_length_hint = std::max(
            batch.sequence_length_hint,
            request.spec.prompt_tokens + request.completed_output_tokens);
        total_tokens += request_tokens;
        if (batch.items.size() >= request_limit) {
            break;
        }
    }

    if (batch.items.empty()) {
        return std::nullopt;
    }
    return batch;
}

std::optional<ServingBatch> ServingBatchBuilder::build_decode_from_running(
    uint64_t batch_id,
    ServingStageType stage,
    size_t group_id,
    size_t replica_id,
    const ParallelismLayoutSpec& layout,
    const std::vector<size_t>& running_requests,
    const std::vector<ServingRequestState>& requests,
    uint64_t request_limit) {
    auto batch = make_base_batch(batch_id, stage, group_id, replica_id, layout);

    for (const auto request_index : running_requests) {
        const auto& request = requests[request_index];
        if (request.replica_id != replica_id || request.finished_recorded ||
            request.in_active_batch || request.remaining_prefill_tokens > 0 ||
            request.completed_output_tokens >= request.spec.output_tokens) {
            continue;
        }
        batch.items.push_back(ServingBatchItem{request_index, 1});
        batch.include_base_latency =
            batch.include_base_latency || request.completed_output_tokens == 0;
        batch.sequence_length_hint = std::max(
            batch.sequence_length_hint,
            request.spec.prompt_tokens + request.completed_output_tokens + 1);
        if (batch.items.size() >= request_limit) {
            break;
        }
    }

    if (batch.items.empty()) {
        return std::nullopt;
    }
    return batch;
}

std::optional<ServingBatch> ServingBatchBuilder::build_prefill_from_queue(
    uint64_t batch_id,
    ServingStageType stage,
    size_t group_id,
    size_t replica_id,
    const ParallelismLayoutSpec& layout,
    std::deque<size_t>& queue,
    const std::vector<ServingRequestState>& requests,
    const ServingPdConfig& pd) {
    if (queue.empty()) {
        return std::nullopt;
    }

    auto batch = make_base_batch(batch_id, stage, group_id, replica_id, layout);
    const auto token_limit = pd_prefill_token_limit(pd);
    const auto request_limit = pd.prefill_max_requests;
    uint64_t total_tokens = 0;
    std::deque<size_t> retained;

    while (!queue.empty() && batch.items.size() < request_limit) {
        const auto request_index = queue.front();
        queue.pop_front();
        const auto& request = requests[request_index];
        if (request.replica_id != replica_id) {
            retained.push_back(request_index);
            continue;
        }
        const auto request_tokens = request.remaining_prefill_tokens;
        const bool fits =
            batch.items.empty() || total_tokens + request_tokens <= token_limit;
        if (!fits) {
            retained.push_back(request_index);
            continue;
        }
        batch.items.push_back(ServingBatchItem{request_index, request_tokens});
        batch.sequence_length_hint = std::max(
            batch.sequence_length_hint, request.spec.prompt_tokens);
        total_tokens += request_tokens;
    }

    while (!retained.empty()) {
        queue.push_front(retained.back());
        retained.pop_back();
    }

    if (batch.items.empty()) {
        return std::nullopt;
    }
    return batch;
}

std::optional<ServingBatch> ServingBatchBuilder::build_decode_from_queue(
    uint64_t batch_id,
    ServingStageType stage,
    size_t group_id,
    size_t replica_id,
    const ParallelismLayoutSpec& layout,
    std::deque<size_t>& queue,
    const std::vector<ServingRequestState>& requests,
    uint64_t request_limit) {
    if (queue.empty()) {
        return std::nullopt;
    }

    auto batch = make_base_batch(batch_id, stage, group_id, replica_id, layout);
    std::deque<size_t> retained;

    while (!queue.empty() && batch.items.size() < request_limit) {
        const auto request_index = queue.front();
        queue.pop_front();
        const auto& request = requests[request_index];
        if (request.replica_id != replica_id || request.finished_recorded ||
            request.in_active_batch) {
            retained.push_back(request_index);
            continue;
        }
        batch.items.push_back(ServingBatchItem{request_index, 1});
        batch.include_base_latency =
            batch.include_base_latency || request.completed_output_tokens == 0;
        batch.sequence_length_hint = std::max(
            batch.sequence_length_hint,
            request.spec.prompt_tokens + request.completed_output_tokens + 1);
    }

    while (!retained.empty()) {
        queue.push_front(retained.back());
        retained.pop_back();
    }

    if (batch.items.empty()) {
        return std::nullopt;
    }
    return batch;
}

}  // namespace AstraSim
