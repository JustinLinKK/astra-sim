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

echo "[$0] Running single-request PD case..."
run_case single_pd_no_transfer.json ${SCRIPT_DIR}/outputs/no_transfer_summary.json ${SCRIPT_DIR}/outputs/no_transfer_metrics.csv

echo "[$0] Running PD transfer case..."
run_case single_pd_with_transfer.json ${SCRIPT_DIR}/outputs/with_transfer_summary.json ${SCRIPT_DIR}/outputs/with_transfer_metrics.csv

echo "[$0] Running PD fast-transfer case..."
run_case single_pd_fast_transfer.json ${SCRIPT_DIR}/outputs/fast_transfer_summary.json ${SCRIPT_DIR}/outputs/fast_transfer_metrics.csv

echo "[$0] Running PD chunk split case..."
run_case pd_chunk_split_math.json ${SCRIPT_DIR}/outputs/pd_chunk_split_summary.json ${SCRIPT_DIR}/outputs/pd_chunk_split_metrics.csv

echo "[$0] Running PD chunk requeue metrics case..."
run_case pd_chunk_requeue_metrics.json ${SCRIPT_DIR}/outputs/pd_chunk_requeue_summary.json ${SCRIPT_DIR}/outputs/pd_chunk_requeue_metrics.csv

echo "[$0] Running PD pipeline overlap case..."
run_case pd_two_stage_pipeline.json ${SCRIPT_DIR}/outputs/pipeline_summary.json ${SCRIPT_DIR}/outputs/pipeline_metrics.csv

echo "[$0] Running PD decode-worker split cases..."
run_case pd_worker_split_decode1.json ${SCRIPT_DIR}/outputs/decode1_summary.json ${SCRIPT_DIR}/outputs/decode1_metrics.csv
run_case pd_worker_split_decode2.json ${SCRIPT_DIR}/outputs/decode2_summary.json ${SCRIPT_DIR}/outputs/decode2_metrics.csv

echo "[$0] Running fixed-seed PD case twice for determinism..."
run_case seeded_trace_pd.json ${SCRIPT_DIR}/outputs/seeded_summary_1.json ${SCRIPT_DIR}/outputs/seeded_metrics_1.csv ${SCRIPT_DIR}/outputs/seeded_metadata_1.json
run_case seeded_trace_pd.json ${SCRIPT_DIR}/outputs/seeded_summary_2.json ${SCRIPT_DIR}/outputs/seeded_metrics_2.csv ${SCRIPT_DIR}/outputs/seeded_metadata_2.json

python3 - \
    "${SCRIPT_DIR}/outputs/no_transfer_metrics.csv" \
    "${SCRIPT_DIR}/outputs/with_transfer_metrics.csv" \
    "${SCRIPT_DIR}/outputs/fast_transfer_metrics.csv" \
    "${SCRIPT_DIR}/outputs/pd_chunk_split_events.csv" \
    "${SCRIPT_DIR}/outputs/pd_chunk_split_stage_metrics.csv" \
    "${SCRIPT_DIR}/outputs/pd_chunk_split_metrics.csv" \
    "${SCRIPT_DIR}/outputs/pd_chunk_requeue_metrics.csv" \
    "${SCRIPT_DIR}/outputs/pipeline_metrics.csv" \
    "${SCRIPT_DIR}/outputs/decode1_summary.json" \
    "${SCRIPT_DIR}/outputs/decode2_summary.json" \
    "${SCRIPT_DIR}/outputs/seeded_summary_1.json" \
    "${SCRIPT_DIR}/outputs/seeded_summary_2.json" \
    "${SCRIPT_DIR}/outputs/seeded_metrics_1.csv" \
    "${SCRIPT_DIR}/outputs/seeded_metrics_2.csv" \
    "${SCRIPT_DIR}/outputs/seeded_metadata_1.json" \
    "${SCRIPT_DIR}/outputs/seeded_metadata_2.json" <<'PY'
import csv
import json
import sys

no_transfer = list(csv.DictReader(open(sys.argv[1])))
with_transfer = list(csv.DictReader(open(sys.argv[2])))
fast_transfer = list(csv.DictReader(open(sys.argv[3])))
chunk_split_events = list(csv.DictReader(open(sys.argv[4])))
chunk_split_stage_metrics = list(csv.DictReader(open(sys.argv[5])))
chunk_split_metrics = list(csv.DictReader(open(sys.argv[6])))
chunk_requeue_metrics = list(csv.DictReader(open(sys.argv[7])))
pipeline = list(csv.DictReader(open(sys.argv[8])))
decode1 = json.load(open(sys.argv[9]))
decode2 = json.load(open(sys.argv[10]))
seeded_summary_1 = json.load(open(sys.argv[11]))
seeded_summary_2 = json.load(open(sys.argv[12]))
seeded_metrics_1 = open(sys.argv[13]).read()
seeded_metrics_2 = open(sys.argv[14]).read()
seeded_metadata_1 = json.load(open(sys.argv[15]))
seeded_metadata_2 = json.load(open(sys.argv[16]))

assert no_transfer[0]["ttft_ns"] == "1500"
assert no_transfer[0]["transfer_duration_ns"] == "0"

assert with_transfer[0]["transfer_duration_ns"] == "4000"
assert with_transfer[0]["prefill_start_ns"] == "0"
assert with_transfer[0]["prefill_end_time_ns"] == "1000"
assert with_transfer[0]["transfer_start_ns"] == "1000"
assert with_transfer[0]["transfer_end_ns"] == "5000"
assert with_transfer[0]["decode_start_ns"] == "5000"
assert with_transfer[0]["ttft_ns"] == "5500"
assert with_transfer[0]["e2e_ns"] == "5500"

assert fast_transfer[0]["transfer_duration_ns"] == "3500"
assert int(fast_transfer[0]["ttft_ns"]) < int(with_transfer[0]["ttft_ns"])
assert int(fast_transfer[0]["e2e_ns"]) < int(with_transfer[0]["e2e_ns"])

chunk_split_prefill = [
    int(row["total_tokens"])
    for row in chunk_split_events
    if row["event"] == "batch_scheduled" and row["stage"] == "pd_prefill"
]
assert chunk_split_prefill == [2, 2, 1]
assert chunk_split_metrics[0]["prefill_chunk_count"] == "3"
assert chunk_split_metrics[0]["prefill_service_ns"] == "5000"
assert chunk_split_metrics[0]["total_prefill_queue_wait_ns"] == "0"
assert chunk_split_metrics[0]["max_prefill_chunk_tokens"] == "2"

chunk_split_stage_prefill = [
    int(row["total_tokens"])
    for row in chunk_split_stage_metrics
    if row["stage"] == "pd_prefill"
]
assert chunk_split_stage_prefill == [2, 2, 1]
decode_rows = [row for row in chunk_split_stage_metrics if row["stage"] == "pd_decode"]
assert len(decode_rows) == 1
assert decode_rows[0]["include_base_latency"] == "true"
assert "prefill_queue_depth" in chunk_split_stage_metrics[0]

long_request = next(row for row in chunk_requeue_metrics if row["request_id"] == "1")
assert long_request["prefill_chunk_count"] == "3"
assert long_request["prefill_duration_ns"] == "6000"
assert long_request["prefill_service_ns"] == "5000"
assert long_request["prefill_stage_wait_ns"] == "1000"
assert long_request["prefill_queue_delay_ns"] == "1000"
assert long_request["total_prefill_queue_wait_ns"] == "2000"

assert int(pipeline[1]["prefill_start_ns"]) < int(pipeline[0]["finish_time_ns"])
assert pipeline[1]["prefill_end_time_ns"] == "2000"
assert pipeline[1]["decode_start_ns"] == "2000"

assert decode2["output_token_throughput_tokens_per_sec"] > decode1["output_token_throughput_tokens_per_sec"]
assert decode2["e2e_ns"]["mean"] < decode1["e2e_ns"]["mean"]

assert seeded_summary_1 == seeded_summary_2
assert seeded_metrics_1 == seeded_metrics_2
seeded_metadata_1.pop("timestamp_utc", None)
seeded_metadata_2.pop("timestamp_utc", None)
assert seeded_metadata_1 == seeded_metadata_2
assert seeded_metadata_1["architecture"] == "pd_disaggregated"
assert seeded_metadata_1["runtime_seed"] == 23
assert seeded_metadata_1["trace_seed"] == 11
PY

echo "[$0] Ok."
