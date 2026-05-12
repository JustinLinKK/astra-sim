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

echo "[$0] Running chunk split case..."
run_case chunk_split_math.json ${SCRIPT_DIR}/outputs/chunk_split_summary.json ${SCRIPT_DIR}/outputs/chunk_split_metrics.csv

echo "[$0] Running chunk interleaving case..."
run_case chunk_interleaving.json ${SCRIPT_DIR}/outputs/chunk_interleaving_summary.json ${SCRIPT_DIR}/outputs/chunk_interleaving_metrics.csv

echo "[$0] Running fixed-seed chunked case twice for determinism..."
run_case seeded_trace_chunked.json ${SCRIPT_DIR}/outputs/seeded_summary_1.json ${SCRIPT_DIR}/outputs/seeded_metrics_1.csv ${SCRIPT_DIR}/outputs/seeded_metadata_1.json
run_case seeded_trace_chunked.json ${SCRIPT_DIR}/outputs/seeded_summary_2.json ${SCRIPT_DIR}/outputs/seeded_metrics_2.csv ${SCRIPT_DIR}/outputs/seeded_metadata_2.json

python3 - \
    "${SCRIPT_DIR}/outputs/chunk_split_events.csv" \
    "${SCRIPT_DIR}/outputs/chunk_interleaving_events.csv" \
    "${SCRIPT_DIR}/outputs/seeded_summary_1.json" \
    "${SCRIPT_DIR}/outputs/seeded_summary_2.json" \
    "${SCRIPT_DIR}/outputs/seeded_metrics_1.csv" \
    "${SCRIPT_DIR}/outputs/seeded_metrics_2.csv" \
    "${SCRIPT_DIR}/outputs/seeded_metadata_1.json" \
    "${SCRIPT_DIR}/outputs/seeded_metadata_2.json" <<'PY'
import csv
import json
import sys

split_events = list(csv.DictReader(open(sys.argv[1])))
interleave_events = list(csv.DictReader(open(sys.argv[2])))
seeded_summary_1 = json.load(open(sys.argv[3]))
seeded_summary_2 = json.load(open(sys.argv[4]))
seeded_metrics_1 = open(sys.argv[5]).read()
seeded_metrics_2 = open(sys.argv[6]).read()
seeded_metadata_1 = json.load(open(sys.argv[7]))
seeded_metadata_2 = json.load(open(sys.argv[8]))

split_prefill = [
    int(row["total_tokens"])
    for row in split_events
    if row["event"] == "batch_scheduled" and row["stage"] == "colocated_prefill"
]
assert split_prefill == [2, 2, 1]
assert sum(split_prefill) == 5

first_long_prefill = next(
    int(row["time_ns"])
    for row in interleave_events
    if row["event"] == "batch_scheduled"
    and row["stage"] == "colocated_prefill"
    and row["request_ids"] == "1"
)
short_finish = next(
    int(row["time_ns"])
    for row in interleave_events
    if row["event"] == "request_finished" and row["request_ids"] == "0"
)
assert first_long_prefill < short_finish

assert seeded_summary_1 == seeded_summary_2
assert seeded_metrics_1 == seeded_metrics_2
seeded_metadata_1.pop("timestamp_utc", None)
seeded_metadata_2.pop("timestamp_utc", None)
assert seeded_metadata_1 == seeded_metadata_2
assert seeded_metadata_1["architecture"] == "colocated_chunked"
assert seeded_metadata_1["runtime_seed"] == 19
assert seeded_metadata_1["trace_seed"] == 13
PY

echo "[$0] Ok."
