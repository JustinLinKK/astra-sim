#!/bin/bash
set -e

SCRIPT_DIR=$(dirname "$(realpath "$0")")
ASTRA_SIM_BIN=${SCRIPT_DIR}/../../build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Aware
BUILD_SCRIPT=${SCRIPT_DIR}/../../build/astra_analytical/build.sh

rm -rf ${SCRIPT_DIR}/outputs/*
mkdir -p ${SCRIPT_DIR}/outputs

echo "[$0] Building ASTRA-sim..."
${BUILD_SCRIPT}

clean_log() {
    sed -E 's/\[[^]]+\] //; s/\[[^]]+\] //; s/\[[^]]+\] //'
}

run_case() {
    local request_cfg=$1
    local stdout_name=$2
    local metrics_output=$3
    local summary_output=$4
    local metadata_output=$5
    local config_path=${SCRIPT_DIR}/outputs/${request_cfg%.json}.yaml

    cat > ${config_path} <<EOF
mode: serving_disagg_colocated
request_configuration: ${SCRIPT_DIR}/inputs/${request_cfg}
request_metrics_output: ${metrics_output}
request_summary_output: ${summary_output}
request_run_metadata_output: ${metadata_output}
workload_configuration: empty
comm_group_configuration: empty
system_configuration: ${SCRIPT_DIR}/inputs/system_cfg.json
remote_memory_configuration: ${SCRIPT_DIR}/inputs/remote_memory_cfg.json
network_configuration: ${SCRIPT_DIR}/inputs/network_cfg.yml
logging_configuration: empty
logging_folder: ${SCRIPT_DIR}/outputs/log
num_queues_per_dim: 1
compute_scale: 1.0
comm_scale: 1.0
injection_scale: 1.0
rendezvous_protocol: false
EOF

    ${ASTRA_SIM_BIN} \
        --analytical-config=${config_path} | tee ${SCRIPT_DIR}/outputs/${stdout_name}
}

echo "[$0] Running single-request scenario..."
run_case single_no_collective.json single_stdout.txt empty empty empty
clean_log < ${SCRIPT_DIR}/outputs/single_stdout.txt > ${SCRIPT_DIR}/outputs/single_stdout_clean.txt
diff ${SCRIPT_DIR}/outputs/single_stdout_clean.txt ${SCRIPT_DIR}/refs/single_stdout.txt || (echo "Failed." ; exit 1)

echo "[$0] Running overlapping-request scenario..."
run_case overlapping_requests.json overlapping_stdout.txt ${SCRIPT_DIR}/outputs/overlapping_metrics.csv ${SCRIPT_DIR}/outputs/overlapping_summary.json ${SCRIPT_DIR}/outputs/overlapping_metadata.json
clean_log < ${SCRIPT_DIR}/outputs/overlapping_stdout.txt > ${SCRIPT_DIR}/outputs/overlapping_stdout_clean.txt
diff ${SCRIPT_DIR}/outputs/overlapping_stdout_clean.txt ${SCRIPT_DIR}/refs/overlapping_stdout.txt || (echo "Failed." ; exit 1)
diff ${SCRIPT_DIR}/outputs/overlapping_metrics.csv ${SCRIPT_DIR}/refs/overlapping_metrics.csv || (echo "Failed." ; exit 1)

python3 - "${SCRIPT_DIR}/outputs/overlapping_summary.json" "${SCRIPT_DIR}/outputs/overlapping_metadata.json" <<'PY'
import json
import sys

summary_path, metadata_path = sys.argv[1:3]
summary = json.load(open(summary_path))
metadata = json.load(open(metadata_path))

assert summary["num_requests"] == 2
assert summary["architecture"] == "serial_baseline"
assert summary["queue_delay_ns"]["mean"] > 0
assert summary["ttft_ns"]["p99"] >= summary["ttft_ns"]["p50"]
assert summary["goodput"]["good_requests"] == 2
assert metadata["seed"] is None
assert metadata["architecture"] == "serial_baseline"
assert metadata["simulator_version"] == "serving-week2-v1"
PY

echo "[$0] Running collective-backed scenario..."
run_case single_with_collective.json collective_stdout.txt empty empty empty
clean_log < ${SCRIPT_DIR}/outputs/collective_stdout.txt > ${SCRIPT_DIR}/outputs/collective_stdout_clean.txt
diff ${SCRIPT_DIR}/outputs/collective_stdout_clean.txt ${SCRIPT_DIR}/refs/collective_stdout.txt || (echo "Failed." ; exit 1)

echo "[$0] Running deterministic acceptance scenario..."
run_case deterministic_acceptance.json acceptance_stdout.txt ${SCRIPT_DIR}/outputs/acceptance_metrics.csv ${SCRIPT_DIR}/outputs/acceptance_summary.json ${SCRIPT_DIR}/outputs/acceptance_metadata.json
clean_log < ${SCRIPT_DIR}/outputs/acceptance_stdout.txt > ${SCRIPT_DIR}/outputs/acceptance_stdout_clean.txt
diff ${SCRIPT_DIR}/outputs/acceptance_stdout_clean.txt ${SCRIPT_DIR}/refs/acceptance_stdout.txt || (echo "Failed." ; exit 1)
diff ${SCRIPT_DIR}/outputs/acceptance_metrics.csv ${SCRIPT_DIR}/refs/acceptance_metrics.csv || (echo "Failed." ; exit 1)
diff ${SCRIPT_DIR}/outputs/acceptance_summary.json ${SCRIPT_DIR}/refs/acceptance_summary.json || (echo "Failed." ; exit 1)

python3 - "${SCRIPT_DIR}/outputs/acceptance_metadata.json" <<'PY'
import json
import sys

metadata = json.load(open(sys.argv[1]))
assert metadata["seed"] is None
assert metadata["git_commit"]
assert metadata["architecture"] == "serial_baseline"
PY

echo "[$0] Running generated low-load trace twice for determinism..."
run_case generated_low_load.json generated_low_stdout_1.txt ${SCRIPT_DIR}/outputs/generated_low_1.csv ${SCRIPT_DIR}/outputs/generated_low_1.json ${SCRIPT_DIR}/outputs/generated_low_1.metadata.json
run_case generated_low_load.json generated_low_stdout_2.txt ${SCRIPT_DIR}/outputs/generated_low_2.csv ${SCRIPT_DIR}/outputs/generated_low_2.json ${SCRIPT_DIR}/outputs/generated_low_2.metadata.json
diff ${SCRIPT_DIR}/outputs/generated_low_1.csv ${SCRIPT_DIR}/outputs/generated_low_2.csv || (echo "Failed." ; exit 1)
diff ${SCRIPT_DIR}/outputs/generated_low_1.json ${SCRIPT_DIR}/outputs/generated_low_2.json || (echo "Failed." ; exit 1)

python3 - "${SCRIPT_DIR}/outputs/generated_low_1.json" "${SCRIPT_DIR}/outputs/generated_low_1.metadata.json" <<'PY'
import json
import sys

summary = json.load(open(sys.argv[1]))
metadata = json.load(open(sys.argv[2]))
assert summary["num_requests"] == 64
assert summary["architecture"] == "serial_baseline"
assert metadata["seed"] == 7
assert metadata["config_path"].endswith("generated_low_load.json")
PY

echo "[$0] Running generated high-load trace..."
run_case generated_high_load.json generated_high_stdout.txt empty ${SCRIPT_DIR}/outputs/generated_high.json empty

echo "[$0] Running short-prompt and long-prompt traces..."
run_case generated_short_prompts.json generated_short_stdout.txt empty ${SCRIPT_DIR}/outputs/generated_short.json empty
run_case generated_long_prompts.json generated_long_stdout.txt empty ${SCRIPT_DIR}/outputs/generated_long.json empty

python3 - \
    "${SCRIPT_DIR}/outputs/generated_low_1.json" \
    "${SCRIPT_DIR}/outputs/generated_high.json" \
    "${SCRIPT_DIR}/outputs/generated_short.json" \
    "${SCRIPT_DIR}/outputs/generated_long.json" <<'PY'
import json
import sys

low, high, short, long = [json.load(open(path)) for path in sys.argv[1:5]]

assert high["queue_delay_ns"]["mean"] > low["queue_delay_ns"]["mean"]
assert high["queue_delay_ns"]["p90"] > low["queue_delay_ns"]["p90"]
assert long["ttft_ns"]["mean"] > short["ttft_ns"]["mean"]
assert abs(long["tpot_ns"]["mean"] - short["tpot_ns"]["mean"]) < 1e-9
PY

echo "[$0] Ok."
