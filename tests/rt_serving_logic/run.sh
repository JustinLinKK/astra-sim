#!/bin/bash
set -e

SCRIPT_DIR=$(dirname "$(realpath "$0")")
BUILD_SCRIPT=${SCRIPT_DIR}/../../build/astra_analytical/build.sh
TEST_BIN=${SCRIPT_DIR}/../../build/astra_analytical/build/bin/AstraSim_Serving_Logic_Tests

echo "[$0] Building ASTRA-sim..."
${BUILD_SCRIPT}

echo "[$0] Running serving logic tests..."
${TEST_BIN}

echo "[$0] Ok."
