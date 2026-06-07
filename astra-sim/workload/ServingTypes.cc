#include "astra-sim/workload/ServingTypes.hh"

namespace AstraSim {

std::string to_string(ServingArchitecture architecture) {
    switch (architecture) {
    case ServingArchitecture::SerialBaseline:
        return "serial_baseline";
    case ServingArchitecture::Colocated:
        return "colocated";
    case ServingArchitecture::ColocatedChunked:
        return "colocated_chunked";
    case ServingArchitecture::PdDisaggregated:
        return "pd_disaggregated";
    }
    return "unknown";
}

std::string to_string(ServingSchedulerPolicy policy) {
    switch (policy) {
    case ServingSchedulerPolicy::Serial:
        return "serial";
    case ServingSchedulerPolicy::DecodeFirst:
        return "decode_first";
    case ServingSchedulerPolicy::PrefillFirst:
        return "prefill_first";
    case ServingSchedulerPolicy::Balanced:
        return "balanced";
    case ServingSchedulerPolicy::Fcfs:
        return "fcfs";
    }
    return "unknown";
}

std::string to_string(ServingFirstTokenTiming timing) {
    switch (timing) {
    case ServingFirstTokenTiming::DecodeEnd:
        return "decode_end";
    case ServingFirstTokenTiming::DecodeStart:
        return "decode_start";
    }
    return "unknown";
}

std::string to_string(RequestPhase phase) {
    switch (phase) {
    case RequestPhase::Arrived:
        return "arrived";
    case RequestPhase::WaitingForAdmission:
        return "waiting_for_admission";
    case RequestPhase::PrefillQueued:
        return "prefill_queued";
    case RequestPhase::PrefillRunning:
        return "prefill_running";
    case RequestPhase::TransferQueued:
        return "transfer_queued";
    case RequestPhase::TransferRunning:
        return "transfer_running";
    case RequestPhase::DecodeQueued:
        return "decode_queued";
    case RequestPhase::DecodeRunning:
        return "decode_running";
    case RequestPhase::Finished:
        return "finished";
    }
    return "unknown";
}

std::string to_string(ServingStageType stage) {
    switch (stage) {
    case ServingStageType::RequestArrival:
        return "request_arrival";
    case ServingStageType::SerialPrefill:
        return "serial_prefill";
    case ServingStageType::SerialDecode:
        return "serial_decode";
    case ServingStageType::ColocatedPrefill:
        return "colocated_prefill";
    case ServingStageType::ColocatedDecode:
        return "colocated_decode";
    case ServingStageType::PdPrefill:
        return "pd_prefill";
    case ServingStageType::PdTransfer:
        return "pd_transfer";
    case ServingStageType::PdDecode:
        return "pd_decode";
    }
    return "unknown";
}

std::string to_string(ServingWorkerRole role) {
    switch (role) {
    case ServingWorkerRole::Colocated:
        return "colocated";
    case ServingWorkerRole::Prefill:
        return "prefill";
    case ServingWorkerRole::Decode:
        return "decode";
    }
    return "unknown";
}

std::string to_string(ServingWorkerGroupRole role) {
    switch (role) {
    case ServingWorkerGroupRole::Colocated:
        return "colocated";
    case ServingWorkerGroupRole::Prefill:
        return "prefill";
    case ServingWorkerGroupRole::Decode:
        return "decode";
    case ServingWorkerGroupRole::Expert:
        return "expert";
    }
    return "unknown";
}

}  // namespace AstraSim
