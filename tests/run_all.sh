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

echo "[$0] Running rt_analytical..."
${SCRIPT_DIR}/rt_analytical/run.sh || (echo "Failed." ; exit 1)

echo "[$0] Finished all regression tests."
