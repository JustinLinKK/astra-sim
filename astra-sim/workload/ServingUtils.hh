#ifndef __SERVING_UTILS_HH__
#define __SERVING_UTILS_HH__

#include <cstddef>
#include <cstdint>
#include <vector>

#include "astra-sim/system/Common.hh"

namespace AstraSim {

struct ServingMetricStats {
    size_t count;
    double mean;
    double p50;
    double p90;
    double p99;
    double max;
};

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

}  // namespace AstraSim

#endif /* __SERVING_UTILS_HH__ */
