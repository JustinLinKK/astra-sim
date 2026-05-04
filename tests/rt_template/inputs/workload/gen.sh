#!/bin/bash
set -e

# Path
SCRIPT_DIR=$(dirname "$(realpath $0)")
REPO_ROOT=$(realpath "${SCRIPT_DIR}/../../../..")

cd ${SCRIPT_DIR}

export PYTHONPATH="${REPO_ROOT}/extern/graph_frontend:${PYTHONPATH}"
python3 ${SCRIPT_DIR}/gen_chakra_traces.py
