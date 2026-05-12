/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <json/json.hpp>

#include "astra-sim/analytical/AnalyticalConfig.hh"
#include "astra-sim/analytical/AnalyticalCostModels.hh"
#include "astra-sim/analytical/AttentionFfnDisaggregationModel.hh"
#include "astra-sim/analytical/TpPpCrossoverModel.hh"

using json = nlohmann::json;
using namespace AstraSim;

namespace {

void expect_true(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_close(double lhs, double rhs, double tolerance,
                  const std::string& message) {
    if (std::fabs(lhs - rhs) > tolerance) {
        throw std::runtime_error(message + ": expected " + std::to_string(rhs) +
                                 ", got " + std::to_string(lhs));
    }
}

std::filesystem::path make_temp_path(const std::string& stem,
                                     const std::string& extension) {
    return std::filesystem::temp_directory_path() / (stem + extension);
}

std::vector<std::map<std::string, std::string>> read_csv_rows(
    const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open CSV: " + path.string());
    }

    std::string header_line;
    std::getline(input, header_line);
    std::stringstream header_stream(header_line);
    std::vector<std::string> headers;
    std::string token;
    while (std::getline(header_stream, token, ',')) {
        headers.push_back(token);
    }

    std::vector<std::map<std::string, std::string>> rows;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        std::stringstream line_stream(line);
        std::map<std::string, std::string> row;
        size_t index = 0;
        while (std::getline(line_stream, token, ',')) {
            if (index < headers.size()) {
                row[headers[index]] = token;
            }
            index++;
        }
        rows.push_back(row);
    }
    return rows;
}

const std::map<std::string, std::string>* find_row(
    const std::vector<std::map<std::string, std::string>>& rows,
    const std::map<std::string, std::string>& expected_values) {
    for (const auto& row : rows) {
        bool matches = true;
        for (const auto& [key, value] : expected_values) {
            const auto it = row.find(key);
            if (it == row.end() || it->second != value) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return &row;
        }
    }
    return nullptr;
}

json read_json(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open JSON: " + path.string());
    }
    return json::parse(input);
}

TpPpCrossoverConfig make_tp_pp_config(const std::filesystem::path& csv_path,
                                      const std::filesystem::path& json_path) {
    return TpPpCrossoverConfig{
        DenseModelSpec{"unit-dense",
                       64000000000ULL,
                       16,
                       4096,
                       14336,
                       32,
                       8,
                       4096,
                       2,
                       2,
                       2},
        ClusterSpec{{DeviceSpec{"test-gpu",
                                DeviceType::GPU,
                                8,
                                80ULL * 1024ULL * 1024ULL * 1024ULL,
                                400e12,
                                900e9,
                                700.0,
                                120.0}},
                    2ULL * 1024ULL * 1024ULL * 1024ULL,
                    7},
        {InterconnectSpec{"gpu-fabric",
                          DeviceType::GPU,
                          DeviceType::GPU,
                          900e9,
                          800,
                          true,
                          0.85}},
        AnalyticalOutputPaths{csv_path.string(), json_path.string()},
        TpPpSweepConfig{{1, 2, 4}, {1, 4}, {4}, {512}, {1, 4}},
        0.42,
        0.80,
        0.90,
        8.0,
    };
}

AttentionFfnDisaggregationConfig make_attention_ffn_config(
    const std::filesystem::path& csv_path,
    const std::filesystem::path& json_path) {
    return AttentionFfnDisaggregationConfig{
        MoEModelSpec{"unit-moe",
                     1000000000000ULL,
                     180000000000ULL,
                     20000000000ULL,
                     800000000000ULL,
                     48,
                     8192,
                     64,
                     8,
                     32,
                     2,
                     32768,
                     8192,
                     2,
                     2,
                     2},
        ClusterSpec{{DeviceSpec{"gpu",
                                DeviceType::GPU,
                                4,
                                80ULL * 1024ULL * 1024ULL * 1024ULL,
                                450e12,
                                1000e9,
                                700.0,
                                120.0},
                     DeviceSpec{"lpu",
                                DeviceType::LPU,
                                4,
                                192ULL * 1024ULL * 1024ULL * 1024ULL,
                                250e12,
                                1200e9,
                                350.0,
                                70.0}},
                    4ULL * 1024ULL * 1024ULL * 1024ULL,
                    9},
        {InterconnectSpec{"gpu-to-lpu",
                          DeviceType::GPU,
                          DeviceType::LPU,
                          400e9,
                          3000,
                          true,
                          0.90},
         InterconnectSpec{"gpu-fabric",
                          DeviceType::GPU,
                          DeviceType::GPU,
                          900e9,
                          800,
                          true,
                          0.85}},
        AnalyticalOutputPaths{csv_path.string(), json_path.string()},
        8,
        4096,
        0.42,
        0.35,
        0.55,
        0.90,
    };
}

void test_mode_and_yaml_parsing() {
    const auto tp_pp_config = AnalyticalConfig::load_from_yaml_text(
        R"yaml(
mode: tp_pp_crossover
model:
  preset: 70b_dense
  hidden_size: 4096
  ffn_hidden_size: 14336
  attention_heads: 32
  kv_heads: 8
  num_layers: 16
  parameter_count: 64000000000
  max_sequence_length: 4096
  bytes_per_parameter: 2
  bytes_per_activation: 2
  bytes_per_kv_element: 2
cluster:
  workspace_reserve_bytes: 2147483648
  seed: 5
  devices:
    - name: gpu
      type: GPU
      count: 8
      memory_capacity_bytes: 85899345920
      peak_flops: 4.0e14
      memory_bandwidth_bytes_per_s: 9.0e11
      active_power_w: 700
      idle_power_w: 120
interconnects:
  - name: gpu-fabric
    src_type: GPU
    dst_type: GPU
    bandwidth_bytes_per_s: 9.0e11
    latency_ns: 800
    full_duplex: true
    efficiency: 0.85
outputs:
  results_csv: empty
  summary_json: empty
sweep:
  tp_degrees: [4, 1, 2]
  pp_degrees: [1]
  batch_sizes: [4]
  sequence_lengths: [512]
compute_efficiency: 0.42
all_reduce_efficiency: 0.8
activation_transfer_efficiency: 0.9
activation_multiplier: 8.0
)yaml",
        "<tp_pp>");

    expect_true(tp_pp_config.mode == AnalyticalMode::tp_pp_crossover,
                "analytical mode should parse");
    expect_true(tp_pp_config.tp_pp_crossover.has_value(),
                "tp/pp payload should be present");
    expect_true(tp_pp_config.tp_pp_crossover->model.hidden_size == 4096,
                "preset override should apply");
    expect_true(tp_pp_config.tp_pp_crossover->sweep.tp_degrees.front() == 1 &&
                    tp_pp_config.tp_pp_crossover->sweep.tp_degrees.back() == 4,
                "tp degrees should be sorted");

    const auto serving_config = AnalyticalConfig::load_from_yaml_text(
        R"yaml(
mode: serving_disagg_colocated
request_configuration: requests.json
request_metrics_output: metrics.csv
request_summary_output: summary.json
request_run_metadata_output: metadata.json
workload_configuration: empty
comm_group_configuration: empty
system_configuration: system.json
remote_memory_configuration: memory.json
network_configuration: network.yml
logging_configuration: empty
logging_folder: log
num_queues_per_dim: 1
compute_scale: 1.0
comm_scale: 1.0
injection_scale: 1.0
rendezvous_protocol: false
)yaml",
        "<serving>");
    expect_true(
        serving_config.mode == AnalyticalMode::serving_disagg_colocated,
        "serving mode should parse");
    expect_true(serving_config.serving_disagg_colocated.has_value(),
                "serving payload should be present");
    expect_true(
        serving_config.serving_disagg_colocated->request_configuration ==
            std::filesystem::absolute("requests.json").string(),
        "serving request configuration should resolve relative paths");

    const auto serving_scale_config = AnalyticalConfig::load_from_yaml_text(
        R"yaml(
mode: serving_scale
dense_model:
  preset: 70b_dense
cluster:
  workspace_reserve_bytes: 2147483648
  devices:
    - name: gpu
      type: GPU
      count: 8
      memory_capacity_bytes: 85899345920
      peak_flops: 9.0e14
      memory_bandwidth_bytes_per_s: 3.0e12
      active_power_w: 700
      idle_power_w: 120
interconnects:
  - name: gpu-fabric
    src_type: GPU
    dst_type: GPU
    bandwidth_bytes_per_s: 9.0e11
    latency_ns: 800
    full_duplex: true
    efficiency: 0.85
topology:
  deployment: pd_disaggregated
  colocated_layout:
    name: colocated
    tp_degree: 1
    pp_degree: 1
    ep_degree: 1
    dp_attention_degree: 1
    dp_replica_count: 2
  prefill_layout:
    name: prefill
    tp_degree: 2
    pp_degree: 1
    ep_degree: 1
    dp_attention_degree: 1
    dp_replica_count: 2
  decode_layout:
    name: decode
    tp_degree: 4
    pp_degree: 2
    ep_degree: 8
    dp_attention_degree: 4
    dp_replica_count: 2
request_configuration: requests.json
request_metrics_output: metrics.csv
request_summary_output: summary.json
request_run_metadata_output: metadata.json
workload_configuration: empty
comm_group_configuration: empty
system_configuration: system.json
remote_memory_configuration: memory.json
network_configuration: network.yml
logging_configuration: empty
logging_folder: log
num_queues_per_dim: 1
compute_scale: 1.0
comm_scale: 1.0
injection_scale: 1.0
rendezvous_protocol: false
)yaml",
        "<serving-scale>");
    expect_true(serving_scale_config.mode == AnalyticalMode::serving_scale,
                "serving_scale mode should parse");
    expect_true(serving_scale_config.serving_scale.has_value(),
                "serving_scale payload should be present");
    expect_true(
        serving_scale_config.serving_scale->dense_model.has_value() &&
            !serving_scale_config.serving_scale->moe_model.has_value(),
        "serving_scale should preserve dense-vs-moe selection");
    expect_true(serving_scale_config.serving_scale->topology.decode_layout
                    .has_value() &&
                    serving_scale_config.serving_scale->topology.decode_layout
                            ->tp_degree == 4,
                "serving_scale decode layout should parse");
    expect_true(serving_scale_config.serving_scale->topology.decode_layout
                    ->dp_attention_degree == 4,
                "serving_scale DP-attention degree should parse");
    expect_true(
        serving_scale_config.serving_scale->request_configuration ==
            std::filesystem::absolute("requests.json").string(),
        "serving_scale request configuration should resolve relative paths");
}

void test_tp_all_reduce_behavior() {
    const auto small =
        ring_all_reduce_time_seconds(1024, 2, 100e9, 1000, 1.0);
    const auto large =
        ring_all_reduce_time_seconds(2048, 2, 100e9, 1000, 1.0);
    const auto wider =
        ring_all_reduce_time_seconds(1024, 4, 100e9, 1000, 1.0);
    expect_true(large > small,
                "all-reduce time should increase with message size");
    expect_true(wider > small,
                "all-reduce time should increase with participant count");
}

void test_pp_pipeline_behavior() {
    const auto csv_path = make_temp_path("tp_pp_pipeline", ".csv");
    const auto json_path = make_temp_path("tp_pp_pipeline", ".json");
    TpPpCrossoverModel model(make_tp_pp_config(csv_path, json_path));
    model.run();

    const auto rows = read_csv_rows(csv_path);
    const auto* no_pipeline = find_row(
        rows,
        {{"tp_degree", "1"}, {"pp_degree", "4"}, {"num_microbatches", "1"}});
    const auto* amortized = find_row(
        rows,
        {{"tp_degree", "1"}, {"pp_degree", "4"}, {"num_microbatches", "4"}});
    expect_true(no_pipeline != nullptr && amortized != nullptr,
                "expected pipeline rows should exist");
    expect_true(std::stoull(amortized->at("latency_ns")) <
                    std::stoull(no_pipeline->at("latency_ns")),
                "more microbatches should amortize pipeline bubbles");
}

void test_memory_feasibility() {
    const ClusterSpec cluster{
        {DeviceSpec{"gpu", DeviceType::GPU, 8, 40ULL * 1024ULL * 1024ULL * 1024ULL,
                    400e12, 900e9, 700.0, 120.0}},
        2ULL * 1024ULL * 1024ULL * 1024ULL,
        std::nullopt,
    };
    const DenseModelSpec model = DenseModelSpec::dense_70b_preset();
    const auto low_batch = estimate_dense_memory_per_gpu(model, cluster, 4, 2, 1,
                                                         512, 1, 8.0);
    const auto high_batch = estimate_dense_memory_per_gpu(model, cluster, 4, 2, 32,
                                                          8192, 1, 8.0);
    expect_true(high_batch.total_bytes > low_batch.total_bytes,
                "memory usage should increase with larger batch and sequence");
    expect_true(low_batch.total_bytes < cluster.devices.front().memory_capacity_bytes,
                "small config should fit");
    expect_true(high_batch.total_bytes > cluster.devices.front().memory_capacity_bytes,
                "large config should not fit");
}

void test_gpu_lpu_placement_transfer_and_energy() {
    const auto csv_path = make_temp_path("attention_ffn_placement", ".csv");
    const auto json_path = make_temp_path("attention_ffn_placement", ".json");
    AttentionFfnDisaggregationModel model(
        make_attention_ffn_config(csv_path, json_path));
    model.run();

    const auto rows = read_csv_rows(csv_path);
    const auto* gpu_row = find_row(rows, {{"placement", "homogeneous_gpu"}});
    const auto* hetero_row =
        find_row(rows, {{"placement", "heterogeneous_gpu_lpu"}});
    expect_true(gpu_row != nullptr && hetero_row != nullptr,
                "expected placement rows should exist");
    expect_close(std::stod(gpu_row->at("lpu_utilization")), 0.0, 1e-9,
                 "homogeneous GPU row should not use LPUs");
    expect_true(std::stoull(hetero_row->at("transfer_overhead_ns")) > 0,
                "heterogeneous placement should include transfer overhead");

    const auto joules_per_token = std::stod(hetero_row->at("joules_per_token"));
    const auto tokens_per_joule = std::stod(hetero_row->at("tokens_per_joule"));
    expect_close(tokens_per_joule, 1.0 / joules_per_token, 1e-6,
                 "energy identities should hold");

    const auto tokens_per_sec = std::stod(hetero_row->at("tokens_per_sec"));
    const auto tokens_per_sec_per_w =
        std::stod(hetero_row->at("tokens_per_sec_per_w"));
    const auto average_power = tokens_per_sec / tokens_per_sec_per_w;
    expect_true(average_power > 0.0, "average power should be positive");

    const auto short_transfer =
        point_to_point_time_seconds(1024, 100e9, 1000, 1.0);
    const auto long_transfer =
        point_to_point_time_seconds(4096, 100e9, 1000, 1.0);
    expect_true(long_transfer > short_transfer,
                "transfer time should scale with payload");

    const auto device = DeviceSpec{"gpu", DeviceType::GPU, 1, 1, 1.0, 1.0,
                                   200.0, 100.0};
    const auto energy = device_energy_joules(1.0, 2.0, device);
    expect_close(energy, 300.0, 1e-9,
                 "energy accounting should combine busy and idle power");
}

void test_deterministic_output_for_fixed_seed() {
    const auto csv_path_a = make_temp_path("tp_pp_seed_a", ".csv");
    const auto json_path_a = make_temp_path("tp_pp_seed_a", ".json");
    const auto csv_path_b = make_temp_path("tp_pp_seed_b", ".csv");
    const auto json_path_b = make_temp_path("tp_pp_seed_b", ".json");

    TpPpCrossoverModel model_a(make_tp_pp_config(csv_path_a, json_path_a));
    TpPpCrossoverModel model_b(make_tp_pp_config(csv_path_b, json_path_b));
    model_a.run();
    model_b.run();

    std::ifstream csv_a(csv_path_a);
    std::ifstream csv_b(csv_path_b);
    std::stringstream csv_a_buffer;
    std::stringstream csv_b_buffer;
    csv_a_buffer << csv_a.rdbuf();
    csv_b_buffer << csv_b.rdbuf();
    expect_true(csv_a_buffer.str() == csv_b_buffer.str(),
                "fixed-seed TP/PP CSV output should be deterministic");

    const auto json_a = read_json(json_path_a);
    const auto json_b = read_json(json_path_b);
    expect_true(json_a.dump() == json_b.dump(),
                "fixed-seed TP/PP JSON output should be deterministic");
}

}  // namespace

int main() {
    try {
        test_mode_and_yaml_parsing();
        test_tp_all_reduce_behavior();
        test_pp_pipeline_behavior();
        test_memory_feasibility();
        test_gpu_lpu_placement_transfer_and_energy();
        test_deterministic_output_for_fixed_seed();
    } catch (const std::exception& error) {
        std::cerr << "Analytical logic test failure: " << error.what() << "\n";
        return 1;
    }

    std::cout << "Analytical logic tests passed.\n";
    return 0;
}
