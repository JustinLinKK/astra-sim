#include "astra-sim/workload/ServingWorkerPool.hh"

namespace AstraSim {

ServingWorkerPool::ServingWorkerPool(size_t count, ServingWorkerRole role) {
    workers.reserve(count);
    for (size_t worker_id = 0; worker_id < count; ++worker_id) {
        workers.push_back(
            ServingWorker{worker_id, role, false, 0, {}});
    }
}

std::optional<size_t> ServingWorkerPool::find_idle() const {
    for (const auto& worker : workers) {
        if (!worker.busy) {
            return worker.worker_id;
        }
    }
    return std::nullopt;
}

void ServingWorkerPool::mark_busy(size_t worker_id, uint64_t batch_id) {
    workers.at(worker_id).busy = true;
    workers.at(worker_id).active_batch_id = batch_id;
}

void ServingWorkerPool::mark_idle(size_t worker_id) {
    workers.at(worker_id).busy = false;
    workers.at(worker_id).active_batch_id = 0;
}

size_t ServingWorkerPool::size() const {
    return workers.size();
}

const std::vector<ServingWorker>& ServingWorkerPool::all_workers() const {
    return workers;
}

}  // namespace AstraSim
