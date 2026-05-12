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
    local config_path=$1
    local stdout_name=$2
    ${ASTRA_SIM_BIN} --analytical-config=${SCRIPT_DIR}/inputs/${config_path} \
        | tee ${SCRIPT_DIR}/outputs/${stdout_name}
}

echo "[$0] Running dense colocated serving_scale case..."
run_case dense_colocated.yaml dense_stdout.txt
clean_log < ${SCRIPT_DIR}/outputs/dense_stdout.txt > ${SCRIPT_DIR}/outputs/dense_stdout_clean.txt
diff ${SCRIPT_DIR}/outputs/dense_stdout_clean.txt ${SCRIPT_DIR}/refs/dense_stdout.txt || (echo "Failed." ; exit 1)
diff ${SCRIPT_DIR}/outputs/dense_metrics.csv ${SCRIPT_DIR}/refs/dense_metrics.csv || (echo "Failed." ; exit 1)
diff ${SCRIPT_DIR}/outputs/dense_summary.json ${SCRIPT_DIR}/refs/dense_summary.json || (echo "Failed." ; exit 1)

python3 - "${SCRIPT_DIR}/outputs/dense_metadata.json" <<'PY'
import json
import sys
metadata = json.load(open(sys.argv[1]))
assert metadata["architecture"] == "colocated"
assert metadata["topology_deployment"] == "colocated"
PY

echo "[$0] Running MoE PD serving_scale case..."
run_case moe_pd.yaml moe_stdout.txt
clean_log < ${SCRIPT_DIR}/outputs/moe_stdout.txt > ${SCRIPT_DIR}/outputs/moe_stdout_clean.txt
diff ${SCRIPT_DIR}/outputs/moe_stdout_clean.txt ${SCRIPT_DIR}/refs/moe_stdout.txt || (echo "Failed." ; exit 1)
diff ${SCRIPT_DIR}/outputs/moe_metrics.csv ${SCRIPT_DIR}/refs/moe_metrics.csv || (echo "Failed." ; exit 1)
diff ${SCRIPT_DIR}/outputs/moe_summary.json ${SCRIPT_DIR}/refs/moe_summary.json || (echo "Failed." ; exit 1)

python3 - "${SCRIPT_DIR}/outputs/moe_metadata.json" <<'PY'
import json
import sys
metadata = json.load(open(sys.argv[1]))
assert metadata["architecture"] == "pd_disaggregated"
assert metadata["topology_deployment"] == "prefill_decode_disaggregated"
PY

echo "[$0] Ok."
