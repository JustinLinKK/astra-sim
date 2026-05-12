#!/bin/bash
set -e

SCRIPT_DIR=$(dirname "$(realpath "$0")")
OUTPUT_DIR=${SCRIPT_DIR}/outputs
FIT_SCRIPT=${SCRIPT_DIR}/../../tools/calibration/fit_serving_cost_model.py

rm -rf ${OUTPUT_DIR}
mkdir -p ${OUTPUT_DIR}

echo "[$0] Fitting calibration anchors..."
${FIT_SCRIPT} \
    --input ${SCRIPT_DIR}/inputs/calibration_anchor.csv \
    --output ${OUTPUT_DIR}/fitted_cost_model.json > ${OUTPUT_DIR}/stdout.txt

python3 - "${OUTPUT_DIR}/fitted_cost_model.json" <<'PY'
import json
import sys

fit = json.load(open(sys.argv[1]))
cost = fit["cost_model"]
stats = fit["fit_stats"]

assert abs(cost["prefill_base_latency_ns"] - 100) <= 1
assert abs(cost["prefill_ns_per_token"] - 20) <= 1
assert abs(cost["decode_base_latency_ns"] - 200) <= 1
assert abs(cost["decode_ns_per_token"] - 5) <= 1
assert stats["prefill_rmse_ns"] <= 1e-9
assert stats["decode_rmse_ns"] <= 1e-9
PY

echo "[$0] Ok."
