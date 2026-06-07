#!/bin/bash
set -e

SCRIPT_DIR=$(dirname "$(realpath "$0")")
ASTRA_SIM_BIN=${SCRIPT_DIR}/../../build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Aware
BUILD_SCRIPT=${SCRIPT_DIR}/../../build/astra_analytical/build.sh

rm -rf ${SCRIPT_DIR}/outputs/*
mkdir -p ${SCRIPT_DIR}/outputs

echo "[$0] Building ASTRA-sim..."
${BUILD_SCRIPT}

run_case() {
    local request_cfg=$1
    local summary_output=$2
    local metrics_output=$3
    local metadata_output=${4:-empty}
    local config_path=${SCRIPT_DIR}/outputs/${request_cfg%.json}.yaml

    cat > ${config_path} <<EOF
mode: serving_disagg_colocated
request_configuration: ${SCRIPT_DIR}/inputs/${request_cfg}
request_metrics_output: ${metrics_output}
request_summary_output: ${summary_output}
request_run_metadata_output: ${metadata_output}
workload_configuration: empty
comm_group_configuration: empty
system_configuration: ${SCRIPT_DIR}/../rt_serving_baseline/inputs/system_cfg.json
remote_memory_configuration: ${SCRIPT_DIR}/../rt_serving_baseline/inputs/remote_memory_cfg.json
network_configuration: ${SCRIPT_DIR}/../rt_serving_baseline/inputs/network_cfg.yml
logging_configuration: empty
logging_folder: ${SCRIPT_DIR}/outputs/log
num_queues_per_dim: 1
compute_scale: 1.0
comm_scale: 1.0
injection_scale: 1.0
rendezvous_protocol: false
EOF

    ${ASTRA_SIM_BIN} --analytical-config=${config_path} > ${SCRIPT_DIR}/outputs/${request_cfg%.json}.stdout.txt
}

echo "[$0] Running single-request colocated case..."
run_case single_request_colocated.json ${SCRIPT_DIR}/outputs/single_summary.json ${SCRIPT_DIR}/outputs/single_metrics.csv

echo "[$0] Running batched prefill case..."
run_case two_request_batch_prefill.json ${SCRIPT_DIR}/outputs/prefill_summary.json ${SCRIPT_DIR}/outputs/prefill_metrics.csv

echo "[$0] Running decode batching case..."
run_case decode_batching.json ${SCRIPT_DIR}/outputs/decode_summary.json ${SCRIPT_DIR}/outputs/decode_metrics.csv

echo "[$0] Running calibration fidelity cases..."
run_case decode_step_batching.json ${SCRIPT_DIR}/outputs/decode_step_summary.json ${SCRIPT_DIR}/outputs/decode_step_metrics.csv
run_case first_token_decode_start.json ${SCRIPT_DIR}/outputs/first_token_summary.json ${SCRIPT_DIR}/outputs/first_token_metrics.csv
run_case fcfs_scheduler.json ${SCRIPT_DIR}/outputs/fcfs_summary.json ${SCRIPT_DIR}/outputs/fcfs_metrics.csv
run_case benchmark_window.json ${SCRIPT_DIR}/outputs/benchmark_window_summary.json ${SCRIPT_DIR}/outputs/benchmark_window_metrics.csv

echo "[$0] Running fixed-seed colocated case twice for determinism..."
run_case seeded_trace_colocated.json ${SCRIPT_DIR}/outputs/seeded_summary_1.json ${SCRIPT_DIR}/outputs/seeded_metrics_1.csv ${SCRIPT_DIR}/outputs/seeded_metadata_1.json
run_case seeded_trace_colocated.json ${SCRIPT_DIR}/outputs/seeded_summary_2.json ${SCRIPT_DIR}/outputs/seeded_metrics_2.csv ${SCRIPT_DIR}/outputs/seeded_metadata_2.json

python3 - \
    "${SCRIPT_DIR}/outputs/single_summary.json" \
    "${SCRIPT_DIR}/outputs/single_metrics.csv" \
    "${SCRIPT_DIR}/outputs/prefill_summary.json" \
    "${SCRIPT_DIR}/outputs/prefill_metrics.csv" \
    "${SCRIPT_DIR}/outputs/decode_metrics.csv" \
    "${SCRIPT_DIR}/outputs/decode_step_metrics.csv" \
    "${SCRIPT_DIR}/outputs/first_token_metrics.csv" \
    "${SCRIPT_DIR}/outputs/fcfs_metrics.csv" \
    "${SCRIPT_DIR}/outputs/benchmark_window_summary.json" \
    "${SCRIPT_DIR}/outputs/seeded_summary_1.json" \
    "${SCRIPT_DIR}/outputs/seeded_summary_2.json" \
    "${SCRIPT_DIR}/outputs/seeded_metrics_1.csv" \
    "${SCRIPT_DIR}/outputs/seeded_metrics_2.csv" \
    "${SCRIPT_DIR}/outputs/seeded_metadata_1.json" \
    "${SCRIPT_DIR}/outputs/seeded_metadata_2.json" <<'PY'
import csv
import json
import math
import sys

(
    single_summary_path,
    single_metrics_path,
    prefill_summary_path,
    prefill_metrics_path,
    decode_metrics_path,
    decode_step_metrics_path,
    first_token_metrics_path,
    fcfs_metrics_path,
    benchmark_window_summary_path,
    seeded_summary_1_path,
    seeded_summary_2_path,
    seeded_metrics_1_path,
    seeded_metrics_2_path,
    seeded_metadata_1_path,
    seeded_metadata_2_path,
) = sys.argv[1:16]

single_summary = json.load(open(single_summary_path))
single_rows = list(csv.DictReader(open(single_metrics_path)))
prefill_summary = json.load(open(prefill_summary_path))
prefill_rows = list(csv.DictReader(open(prefill_metrics_path)))
decode_rows = list(csv.DictReader(open(decode_metrics_path)))
decode_step_rows = list(csv.DictReader(open(decode_step_metrics_path)))
first_token_rows = list(csv.DictReader(open(first_token_metrics_path)))
fcfs_rows = list(csv.DictReader(open(fcfs_metrics_path)))
benchmark_window_summary = json.load(open(benchmark_window_summary_path))
seeded_summary_1 = json.load(open(seeded_summary_1_path))
seeded_summary_2 = json.load(open(seeded_summary_2_path))
seeded_metrics_1 = open(seeded_metrics_1_path).read()
seeded_metrics_2 = open(seeded_metrics_2_path).read()
seeded_metadata_1 = json.load(open(seeded_metadata_1_path))
seeded_metadata_2 = json.load(open(seeded_metadata_2_path))

assert single_summary["architecture"] == "colocated"
assert single_summary["num_requests"] == 1
assert single_rows[0]["ttft_ns"] == "2800"
assert abs(float(single_rows[0]["tpot_ns"]) - 566.67) < 0.01

assert prefill_summary["architecture"] == "colocated"
assert prefill_summary["num_requests"] == 2
assert [row["finish_time_ns"] for row in prefill_rows] == ["3000", "3000"]
assert [row["prefill_duration_ns"] for row in prefill_rows] == ["2000", "2000"]
assert [row["ttft_ns"] for row in prefill_rows] == ["3000", "3000"]

assert [row["first_token_time_ns"] for row in decode_rows] == ["3000", "3000"]
assert [row["finish_time_ns"] for row in decode_rows] == ["4000", "4000"]
assert all(abs(float(row["tpot_ns"]) - 1000.0) < 1e-9 for row in decode_rows)

assert [row["finish_time_ns"] for row in decode_step_rows] == ["2040", "2040"]
assert all(abs(float(row["tpot_ns"]) - 1020.0) < 1e-9 for row in decode_step_rows)

assert first_token_rows[0]["first_token_time_ns"] == "150"
assert first_token_rows[0]["ttft_ns"] == "150"
assert first_token_rows[0]["finish_time_ns"] == "2100"

assert [row["finish_time_ns"] for row in fcfs_rows] == ["5000", "4000"]
assert [row["prefill_start_ns"] for row in fcfs_rows] == ["0", "1000"]
assert [row["decode_start_ns"] for row in fcfs_rows] == ["2000", "3000"]

assert benchmark_window_summary["num_requests"] == 2
assert benchmark_window_summary["benchmark_window"]["configured"] is True
assert benchmark_window_summary["benchmark_window"]["completed_requests"] == 1
assert benchmark_window_summary["goodput"]["good_requests"] == 1
assert benchmark_window_summary["goodput"]["bad_requests"] == 0
assert abs(benchmark_window_summary["goodput"]["goodput_reqs_per_sec"] - 400000.0) < 1e-9
assert abs(benchmark_window_summary["request_throughput_reqs_per_sec"] - 400000.0) < 1e-9

assert seeded_summary_1 == seeded_summary_2
assert seeded_metrics_1 == seeded_metrics_2
seeded_metadata_1.pop("timestamp_utc", None)
seeded_metadata_2.pop("timestamp_utc", None)
assert seeded_metadata_1 == seeded_metadata_2
assert seeded_metadata_1["architecture"] == "colocated"
assert seeded_metadata_1["runtime_seed"] == 17
assert seeded_metadata_1["trace_seed"] == 7
PY

echo "[$0] Ok."
