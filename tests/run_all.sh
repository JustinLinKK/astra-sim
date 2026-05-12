#!/bin/bash
set -e

# Path
SCRIPT_DIR=$(dirname "$(realpath $0)")

echo "[$0] Running all regression tests..."

echo "[$0] Running rt_template..."
${SCRIPT_DIR}/rt_template/run.sh || (echo "Failed." ; exit 1)

echo "[$0] Running rt_serving_logic..."
${SCRIPT_DIR}/rt_serving_logic/run.sh || (echo "Failed." ; exit 1)

echo "[$0] Running rt_serving_baseline..."
${SCRIPT_DIR}/rt_serving_baseline/run.sh || (echo "Failed." ; exit 1)

echo "[$0] Running rt_serving_colocated..."
${SCRIPT_DIR}/rt_serving_colocated/run.sh || (echo "Failed." ; exit 1)

echo "[$0] Running rt_serving_chunked..."
${SCRIPT_DIR}/rt_serving_chunked/run.sh || (echo "Failed." ; exit 1)

echo "[$0] Running rt_serving_pd..."
${SCRIPT_DIR}/rt_serving_pd/run.sh || (echo "Failed." ; exit 1)

echo "[$0] Running rt_serving_goodput..."
${SCRIPT_DIR}/rt_serving_goodput/run.sh || (echo "Failed." ; exit 1)

echo "[$0] Running rt_serving_calibration..."
${SCRIPT_DIR}/rt_serving_calibration/run.sh || (echo "Failed." ; exit 1)

echo "[$0] Running rt_serving_scale..."
${SCRIPT_DIR}/rt_serving_scale/run.sh || (echo "Failed." ; exit 1)

echo "[$0] Running rt_analytical..."
${SCRIPT_DIR}/rt_analytical/run.sh || (echo "Failed." ; exit 1)

echo "[$0] Finished all regression tests."
