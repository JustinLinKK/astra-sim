#ifndef __SERVING_COORDINATOR_HH__
#define __SERVING_COORDINATOR_HH__

#include <memory>
#include <string>
#include <vector>

#include "astra-sim/system/Callable.hh"
#include "astra-sim/system/Sys.hh"
#include "astra-sim/workload/ServingConfig.hh"
#include "astra-sim/workload/ServingRuntime.hh"

namespace AstraSim {

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
    ServingCoordinator(const std::vector<Sys*>& systems,
                       ServingConfig config,
                       const ServingOutputPaths& outputs,
                       const std::string& binary_name,
                       double compute_scale,
                       double comm_scale);

    void fire();
    void call(EventType event, CallData* data) override;

  private:
    std::vector<Sys*> systems;
    Sys* control_sys;
    ServingConfig config;
    ServingOutputPaths outputs;
    std::unique_ptr<ServingRuntime> runtime;
};

}  // namespace AstraSim

#endif /* __SERVING_COORDINATOR_HH__ */
