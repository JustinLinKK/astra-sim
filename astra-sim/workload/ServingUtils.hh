#ifndef __SERVING_UTILS_HH__
#define __SERVING_UTILS_HH__

#include <cstddef>
#include <cstdint>
#include <vector>

#include "astra-sim/system/Common.hh"
#include "astra-sim/workload/ServingConfig.hh"
#include "astra-sim/workload/ServingMetrics.hh"

namespace AstraSim {

Tick calculate_scaled_serving_duration(Tick base_latency_ns,
                                       uint64_t units,
                                       Tick per_unit_latency_ns,
                                       double scale);
double calculate_serving_tpot_ns(Tick decode_duration_ns,
                                 uint64_t output_tokens);
double calculate_throughput_per_second(uint64_t count, Tick makespan_ns);
double calculate_quantile(std::vector<double> values, double quantile);
ServingMetricStats summarize_serving_metric(
    const std::vector<double>& values);
ServingGoodputResult evaluate_serving_goodput(const ServingSloConfig& slo,
                                              Tick ttft_ns,
                                              double tpot_ns,
                                              Tick e2e_ns);

}  // namespace AstraSim

#endif /* __SERVING_UTILS_HH__ */
