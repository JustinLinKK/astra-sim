#!/bin/bash
set -e

SCRIPT_DIR=$(dirname "$(realpath "$0")")
REPO_ROOT=${SCRIPT_DIR}/../..
ASTRA_SIM_BIN=${REPO_ROOT}/build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Aware
BUILD_SCRIPT=${REPO_ROOT}/build/astra_analytical/build.sh

rm -rf ${SCRIPT_DIR}/outputs/*
mkdir -p ${SCRIPT_DIR}/outputs

echo "[$0] Building ASTRA-sim..."
${BUILD_SCRIPT}

echo "[$0] Running TP/PP crossover mode..."
${ASTRA_SIM_BIN} --analytical-config=${REPO_ROOT}/configs/tp_pp_70b.yaml
diff ${SCRIPT_DIR}/outputs/tp_pp_results.csv ${SCRIPT_DIR}/refs/tp_pp_results.csv || (echo "Failed." ; exit 1)
diff ${SCRIPT_DIR}/outputs/tp_pp_summary.json ${SCRIPT_DIR}/refs/tp_pp_summary.json || (echo "Failed." ; exit 1)

echo "[$0] Re-running TP/PP crossover mode for determinism..."
cp ${SCRIPT_DIR}/outputs/tp_pp_results.csv ${SCRIPT_DIR}/outputs/tp_pp_results_run1.csv
cp ${SCRIPT_DIR}/outputs/tp_pp_summary.json ${SCRIPT_DIR}/outputs/tp_pp_summary_run1.json
${ASTRA_SIM_BIN} --analytical-config=${REPO_ROOT}/configs/tp_pp_70b.yaml
diff ${SCRIPT_DIR}/outputs/tp_pp_results_run1.csv ${SCRIPT_DIR}/outputs/tp_pp_results.csv || (echo "Failed." ; exit 1)
diff ${SCRIPT_DIR}/outputs/tp_pp_summary_run1.json ${SCRIPT_DIR}/outputs/tp_pp_summary.json || (echo "Failed." ; exit 1)

echo "[$0] Running attention/FFN disaggregation mode..."
${ASTRA_SIM_BIN} --analytical-config=${REPO_ROOT}/configs/attention_ffn_gpu_lpu_moe.yaml
diff ${SCRIPT_DIR}/outputs/attention_ffn_results.csv ${SCRIPT_DIR}/refs/attention_ffn_results.csv || (echo "Failed." ; exit 1)
diff ${SCRIPT_DIR}/outputs/attention_ffn_summary.json ${SCRIPT_DIR}/refs/attention_ffn_summary.json || (echo "Failed." ; exit 1)

python3 - "${SCRIPT_DIR}/outputs/attention_ffn_results.csv" <<'PY'
import csv
import sys

rows = list(csv.DictReader(open(sys.argv[1])))
assert len(rows) == 2
for row in rows:
    jpt = float(row["joules_per_token"])
    tpj = float(row["tokens_per_joule"])
    tps = float(row["tokens_per_sec"])
    tpspw = float(row["tokens_per_sec_per_w"])
    assert abs(tpj - (1.0 / jpt)) < 1e-6
    assert tpspw > 0
    assert tps / tpspw > 0
PY

echo "[$0] Ok."
