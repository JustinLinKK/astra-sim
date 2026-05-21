#include "astra-sim/workload/ServingCoordinator.hh"

#include <stdexcept>

#include "astra-sim/common/Logging.hh"

namespace AstraSim {

namespace {

[[noreturn]] void serving_runtime_error(const std::string& message) {
    LoggerFactory::get_logger("serving")->critical(message);
    throw std::runtime_error(message);
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
      outputs{request_configuration_path,
              request_metrics_output,
              request_summary_output,
              request_run_metadata_output,
              config.outputs.event_trace_output,
              config.outputs.stage_metrics_output} {
    if (this->control_sys == nullptr) {
        serving_runtime_error(
            "Serving coordinator requires at least one system instance");
    }

    runtime = create_serving_runtime(
        ServingRuntimeContext{this->systems,
                              this->control_sys,
                              this,
                              &this->config,
                              this->outputs,
                              binary_name,
                              compute_scale,
                              comm_scale});
}

ServingCoordinator::ServingCoordinator(const std::vector<Sys*>& systems,
                                       ServingConfig config,
                                       const ServingOutputPaths& outputs,
                                       const std::string& binary_name,
                                       double compute_scale,
                                       double comm_scale)
    : systems(systems),
      control_sys(systems.empty() ? nullptr : systems.front()),
      config(std::move(config)),
      outputs(outputs) {
    if (this->control_sys == nullptr) {
        serving_runtime_error(
            "Serving coordinator requires at least one system instance");
    }

    runtime = create_serving_runtime(
        ServingRuntimeContext{this->systems,
                              this->control_sys,
                              this,
                              &this->config,
                              this->outputs,
                              binary_name,
                              compute_scale,
                              comm_scale});
}

void ServingCoordinator::fire() {
    runtime->fire();
}

void ServingCoordinator::call(EventType event, CallData* data) {
    runtime->call(event, data);
}

}  // namespace AstraSim
