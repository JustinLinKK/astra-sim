#ifndef __SERVING_WORKER_POOL_HH__
#define __SERVING_WORKER_POOL_HH__

#include <optional>
#include <vector>

#include "astra-sim/workload/ServingTypes.hh"

namespace AstraSim {

class ServingWorkerPool {
  public:
    ServingWorkerPool() = default;
    ServingWorkerPool(size_t count, ServingWorkerRole role);

    std::optional<size_t> find_idle() const;
    void mark_busy(size_t worker_id, uint64_t batch_id);
    void mark_idle(size_t worker_id);
    size_t size() const;
    const std::vector<ServingWorker>& all_workers() const;

  private:
    std::vector<ServingWorker> workers;
};

}  // namespace AstraSim

#endif /* __SERVING_WORKER_POOL_HH__ */
