#include "astra-sim/workload/ServingUtils.hh"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace AstraSim {

Tick calculate_scaled_serving_duration(Tick base_latency_ns,
                                       uint64_t units,
                                       Tick per_unit_latency_ns,
                                       double scale) {
    const auto total_delay =
        static_cast<long double>(base_latency_ns) +
        static_cast<long double>(units) *
            static_cast<long double>(per_unit_latency_ns);
    return static_cast<Tick>(std::llround(total_delay * scale));
}

double calculate_serving_tpot_ns(Tick decode_duration_ns,
                                 uint64_t output_tokens) {
    if (output_tokens == 0) {
        return 0.0;
    }
    return static_cast<double>(decode_duration_ns) /
           static_cast<double>(output_tokens);
}

double calculate_throughput_per_second(uint64_t count, Tick makespan_ns) {
    if (makespan_ns == 0) {
        return 0.0;
    }
    return static_cast<double>(count) * 1000000000.0 /
           static_cast<double>(makespan_ns);
}

double calculate_quantile(std::vector<double> values, double quantile) {
    if (values.empty()) {
        return 0.0;
    }

    std::sort(values.begin(), values.end());
    const auto rank = static_cast<size_t>(
        std::ceil(quantile * static_cast<double>(values.size())));
    const auto index = std::max<size_t>(1, rank) - 1;
    return values[std::min(index, values.size() - 1)];
}

ServingMetricStats summarize_serving_metric(const std::vector<double>& values) {
    ServingMetricStats stats{};
    stats.count = values.size();
    if (values.empty()) {
        return stats;
    }

    stats.mean = std::accumulate(values.begin(), values.end(), 0.0) /
                 static_cast<double>(values.size());
    stats.p50 = calculate_quantile(values, 0.50);
    stats.p90 = calculate_quantile(values, 0.90);
    stats.p99 = calculate_quantile(values, 0.99);

    std::vector<double> sorted_values = values;
    std::sort(sorted_values.begin(), sorted_values.end());
    stats.max = sorted_values.back();
    return stats;
}

ServingGoodputResult evaluate_serving_goodput(const ServingSloConfig& slo,
                                              Tick ttft_ns,
                                              double tpot_ns,
                                              Tick e2e_ns) {
    ServingGoodputResult result;
    result.slo_configured = slo.enabled;
    if (!slo.enabled) {
        return result;
    }

    std::vector<std::string> fail_reasons;
    if (slo.ttft_ns > 0) {
        result.ttft_pass = ttft_ns <= slo.ttft_ns;
        if (!result.ttft_pass) {
            fail_reasons.push_back("ttft");
        }
    }
    if (slo.tpot_ns > 0.0) {
        result.tpot_pass = tpot_ns <= slo.tpot_ns;
        if (!result.tpot_pass) {
            fail_reasons.push_back("tpot");
        }
    }
    if (slo.e2e_ns.has_value()) {
        result.e2e_pass = e2e_ns <= *slo.e2e_ns;
        if (!result.e2e_pass) {
            fail_reasons.push_back("e2e");
        }
    }

    result.request_good = fail_reasons.empty();
    if (fail_reasons.empty()) {
        return result;
    }

    for (size_t index = 0; index < fail_reasons.size(); ++index) {
        if (index > 0) {
            result.fail_reason += "+";
        }
        result.fail_reason += fail_reasons[index];
    }
    return result;
}

}  // namespace AstraSim
