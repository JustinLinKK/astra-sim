#include "astra-sim/analytical/TpPpCrossoverModel.hh"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

#include <json/json.hpp>

#include "astra-sim/analytical/AnalyticalCostModels.hh"
#include "astra-sim/analytical/AnalyticalResult.hh"

using json = nlohmann::json;

namespace AstraSim {

namespace {

struct TpPpRow {
    uint32_t tp_degree;
    uint32_t pp_degree;
    uint32_t batch_size;
    uint32_t sequence_length;
    uint32_t num_microbatches;
    double tokens_per_sec;
    uint64_t latency_ns;
    uint64_t compute_time_ns;
    uint64_t communication_time_ns;
    uint64_t memory_per_gpu_bytes;
    bool feasible;
};

struct PureStrategySummary {
    uint32_t degree;
    double tokens_per_sec;
    bool feasible;
};

uint64_t to_ns(double seconds) {
    return static_cast<uint64_t>(std::llround(seconds * 1.0e9));
}

std::string format_double(double value, int precision = 6) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

long double dense_layer_flops(uint32_t batch_size,
                              uint32_t sequence_length,
                              const DenseModelSpec& model) {
    const auto batch = static_cast<long double>(batch_size);
    const auto seq = static_cast<long double>(sequence_length);
    const auto hidden = static_cast<long double>(model.hidden_size);
    const auto ffn_hidden = static_cast<long double>(model.ffn_hidden_size);
    const auto projection_flops = 8.0L * batch * seq * hidden * hidden;
    const auto attention_mix_flops = 4.0L * batch * seq * seq * hidden;
    const auto ffn_flops = 6.0L * batch * seq * hidden * ffn_hidden;
    return projection_flops + attention_mix_flops + ffn_flops;
}

uint64_t activation_transfer_bytes(uint32_t microbatch_batch,
                                   uint32_t sequence_length,
                                   const DenseModelSpec& model) {
    return static_cast<uint64_t>(
        static_cast<long double>(microbatch_batch) *
        static_cast<long double>(sequence_length) *
        static_cast<long double>(model.hidden_size) *
        static_cast<long double>(model.bytes_per_activation));
}

TpPpRow evaluate_row(const TpPpCrossoverConfig& config,
                     uint32_t tp_degree,
                     uint32_t pp_degree,
                     uint32_t batch_size,
                     uint32_t sequence_length,
                     uint32_t num_microbatches,
                     const DeviceSpec& gpu,
                     const InterconnectSpec& gpu_interconnect) {
    TpPpRow row{};
    row.tp_degree = tp_degree;
    row.pp_degree = pp_degree;
    row.batch_size = batch_size;
    row.sequence_length = sequence_length;
    row.num_microbatches = num_microbatches;

    if (tp_degree == 0 || pp_degree == 0 || num_microbatches == 0 ||
        tp_degree * pp_degree > gpu.count) {
        row.feasible = false;
        return row;
    }

    const auto memory = estimate_dense_memory_per_gpu(
        config.model, config.cluster, tp_degree, pp_degree, batch_size,
        sequence_length, num_microbatches, config.activation_multiplier);
    row.memory_per_gpu_bytes = memory.total_bytes;
    row.feasible = memory.total_bytes <= gpu.memory_capacity_bytes;

    const auto microbatch_batch = static_cast<uint32_t>(
        std::ceil(static_cast<long double>(batch_size) /
                  static_cast<long double>(num_microbatches)));
    const auto layer_flops =
        dense_layer_flops(microbatch_batch, sequence_length, config.model);
    const auto base_stage_layers = config.model.num_layers / pp_degree;
    const auto stage_remainder = config.model.num_layers % pp_degree;

    double max_stage_time_seconds = 0.0;
    double full_batch_compute_seconds = 0.0;
    double full_batch_comm_seconds = 0.0;

    for (uint32_t stage_index = 0; stage_index < pp_degree; ++stage_index) {
        const auto stage_layers =
            base_stage_layers + (stage_index < stage_remainder ? 1U : 0U);
        const auto stage_flops =
            layer_flops * static_cast<long double>(stage_layers);
        const auto stage_compute_seconds = compute_time_seconds(
            stage_flops, gpu.peak_flops, config.compute_efficiency, tp_degree);

        double stage_tp_seconds = 0.0;
        if (tp_degree > 1) {
            const auto collective_bytes = activation_transfer_bytes(
                microbatch_batch, sequence_length, config.model);
            stage_tp_seconds = static_cast<double>(stage_layers) * 2.0 *
                               ring_all_reduce_time_seconds(
                                   collective_bytes, tp_degree,
                                   gpu_interconnect.bandwidth_bytes_per_s,
                                   gpu_interconnect.latency_ns,
                                   config.all_reduce_efficiency);
        }

        double stage_activation_seconds = 0.0;
        if (pp_degree > 1 && stage_index + 1 < pp_degree) {
            stage_activation_seconds = point_to_point_time_seconds(
                activation_transfer_bytes(microbatch_batch, sequence_length,
                                          config.model),
                gpu_interconnect.bandwidth_bytes_per_s,
                gpu_interconnect.latency_ns,
                config.activation_transfer_efficiency);
        }

        max_stage_time_seconds = std::max(
            max_stage_time_seconds,
            stage_compute_seconds + stage_tp_seconds + stage_activation_seconds);
        full_batch_compute_seconds +=
            stage_compute_seconds * static_cast<double>(num_microbatches);
        full_batch_comm_seconds +=
            (stage_tp_seconds + stage_activation_seconds) *
            static_cast<double>(num_microbatches);
    }

    double latency_seconds = 0.0;
    if (pp_degree == 1) {
        latency_seconds = max_stage_time_seconds *
                          static_cast<double>(num_microbatches);
    } else {
        latency_seconds = static_cast<double>(num_microbatches + pp_degree - 1) *
                          max_stage_time_seconds;
    }

    row.latency_ns = to_ns(latency_seconds);
    row.compute_time_ns = to_ns(full_batch_compute_seconds);
    row.communication_time_ns = to_ns(full_batch_comm_seconds);
    row.tokens_per_sec =
        latency_seconds > 0.0
            ? static_cast<double>(batch_size) *
                  static_cast<double>(sequence_length) /
                  latency_seconds
            : 0.0;
    return row;
}

PureStrategySummary best_pure_strategy(const std::vector<TpPpRow>& rows,
                                       uint32_t batch_size,
                                       uint32_t sequence_length,
                                       bool pure_tp) {
    PureStrategySummary summary{0, 0.0, false};
    for (const auto& row : rows) {
        if (row.batch_size != batch_size ||
            row.sequence_length != sequence_length || !row.feasible) {
            continue;
        }
        if (pure_tp) {
            if (row.pp_degree != 1) {
                continue;
            }
            if (!summary.feasible || row.tokens_per_sec > summary.tokens_per_sec) {
                summary = PureStrategySummary{row.tp_degree, row.tokens_per_sec, true};
            }
        } else {
            if (row.tp_degree != 1) {
                continue;
            }
            if (!summary.feasible || row.tokens_per_sec > summary.tokens_per_sec) {
                summary = PureStrategySummary{row.pp_degree, row.tokens_per_sec, true};
            }
        }
    }
    return summary;
}

}  // namespace

TpPpCrossoverModel::TpPpCrossoverModel(TpPpCrossoverConfig config)
    : config(std::move(config)) {}

void TpPpCrossoverModel::run() {
    const auto* gpu = config.cluster.find_device(DeviceType::GPU);
    if (gpu == nullptr) {
        throw std::runtime_error(
            "TP/PP crossover mode requires a GPU device spec");
    }
    const auto* gpu_interconnect =
        find_interconnect(config.interconnects, DeviceType::GPU, DeviceType::GPU);
    if (gpu_interconnect == nullptr) {
        throw std::runtime_error(
            "TP/PP crossover mode requires a GPU-to-GPU interconnect spec");
    }

    AnalyticalTable table{
        {"tp_degree",
         "pp_degree",
         "batch_size",
         "sequence_length",
         "num_microbatches",
         "tokens_per_sec",
         "latency_ns",
         "compute_time_ns",
         "communication_time_ns",
         "memory_per_gpu_bytes",
         "feasible"},
        {},
    };

    std::vector<TpPpRow> rows;
    std::vector<uint32_t> microbatch_values = config.sweep.num_microbatches;
    if (microbatch_values.empty()) {
        microbatch_values = config.sweep.batch_sizes;
    }

    for (const auto batch_size : config.sweep.batch_sizes) {
        std::vector<uint32_t> effective_microbatches = microbatch_values;
        if (config.sweep.num_microbatches.empty()) {
            effective_microbatches = {batch_size};
        }
        for (const auto sequence_length : config.sweep.sequence_lengths) {
            for (const auto tp_degree : config.sweep.tp_degrees) {
                for (const auto pp_degree : config.sweep.pp_degrees) {
                    for (const auto num_microbatches : effective_microbatches) {
                        const auto row = evaluate_row(
                            config, tp_degree, pp_degree, batch_size,
                            sequence_length, num_microbatches, *gpu,
                            *gpu_interconnect);
                        rows.push_back(row);
                        table.add_row(
                            {std::to_string(row.tp_degree),
                             std::to_string(row.pp_degree),
                             std::to_string(row.batch_size),
                             std::to_string(row.sequence_length),
                             std::to_string(row.num_microbatches),
                             format_double(row.tokens_per_sec),
                             std::to_string(row.latency_ns),
                             std::to_string(row.compute_time_ns),
                             std::to_string(row.communication_time_ns),
                             std::to_string(row.memory_per_gpu_bytes),
                             row.feasible ? "true" : "false"});
                    }
                }
            }
        }
    }

    json crossover_points = json::array();
    size_t feasible_rows = 0;
    for (const auto& row : rows) {
        if (row.feasible) {
            feasible_rows++;
        }
    }

    for (const auto batch_size : config.sweep.batch_sizes) {
        for (const auto sequence_length : config.sweep.sequence_lengths) {
            const auto best_tp =
                best_pure_strategy(rows, batch_size, sequence_length, true);
            const auto best_pp =
                best_pure_strategy(rows, batch_size, sequence_length, false);
            crossover_points.push_back(
                {{"batch_size", batch_size},
                 {"sequence_length", sequence_length},
                 {"best_pure_tp_degree", best_tp.degree},
                 {"best_pure_tp_tokens_per_sec", best_tp.tokens_per_sec},
                 {"best_pure_pp_degree", best_pp.degree},
                 {"best_pure_pp_tokens_per_sec", best_pp.tokens_per_sec},
                 {"pp_meets_or_exceeds_tp",
                  best_tp.feasible && best_pp.feasible &&
                      best_pp.tokens_per_sec >= best_tp.tokens_per_sec}});
        }
    }

    const json summary{
        {"mode", to_string(AnalyticalMode::tp_pp_crossover)},
        {"model_name", config.model.name},
        {"total_rows", rows.size()},
        {"feasible_rows", feasible_rows},
        {"crossover_points", crossover_points},
        {"seed", config.cluster.seed ? json(*config.cluster.seed) : json(nullptr)},
    };

    AnalyticalResultWriter::write_csv(table, config.outputs.results_csv);
    AnalyticalResultWriter::write_json(summary, config.outputs.summary_json);
}

}  // namespace AstraSim
