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
    local config_path=${SCRIPT_DIR}/outputs/${request_cfg%.json}.yaml

    cat > ${config_path} <<EOF
mode: serving_disagg_colocated
request_configuration: ${SCRIPT_DIR}/inputs/${request_cfg}
request_metrics_output: ${metrics_output}
request_summary_output: ${summary_output}
request_run_metadata_output: empty
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

echo "[$0] Running SLO-mixed goodput case..."
run_case goodput_mixed_slo.json ${SCRIPT_DIR}/outputs/mixed_summary.json ${SCRIPT_DIR}/outputs/mixed_metrics.csv

echo "[$0] Running SLO-disabled goodput case..."
run_case goodput_slo_disabled.json ${SCRIPT_DIR}/outputs/disabled_summary.json ${SCRIPT_DIR}/outputs/disabled_metrics.csv

python3 - \
    "${SCRIPT_DIR}/outputs/mixed_summary.json" \
    "${SCRIPT_DIR}/outputs/mixed_metrics.csv" \
    "${SCRIPT_DIR}/outputs/disabled_summary.json" \
    "${SCRIPT_DIR}/outputs/disabled_metrics.csv" <<'PY'
import csv
import json
import sys

mixed_summary = json.load(open(sys.argv[1]))
mixed_rows = list(csv.DictReader(open(sys.argv[2])))
disabled_summary = json.load(open(sys.argv[3]))
disabled_rows = list(csv.DictReader(open(sys.argv[4])))

assert mixed_summary["goodput"]["good_requests"] == 1
assert mixed_summary["goodput"]["bad_requests"] == 1
assert mixed_rows[0]["request_good"] == "true"
assert mixed_rows[1]["request_good"] == "false"
assert mixed_rows[1]["goodput_fail_reason"] == "ttft"

assert disabled_summary["goodput"]["good_requests"] == 2
assert disabled_summary["goodput"]["bad_requests"] == 0
assert all(row["request_good"] == "true" for row in disabled_rows)
PY

echo "[$0] Ok."
