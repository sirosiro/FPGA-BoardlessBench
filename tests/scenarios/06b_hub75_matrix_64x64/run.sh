#!/bin/bash
set -e

SCENARIO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCENARIO_DIR/../../.." && pwd)"

cd "$PROJECT_ROOT"
CONFIG_FILE="${SCENARIO_DIR}/config.gtr"
if [ ! -f "$CONFIG_FILE" ]; then
    CONFIG_FILE="${SCENARIO_DIR}/config.dts"
fi
PYTHONPATH=scripts python3 scripts/gen_vfpga.py "$CONFIG_FILE"

cd "$PROJECT_ROOT/build"
cmake ..
make -j$(nproc)

echo "[Scenario 06b] Running test_bin..."
if [ -f "$SCENARIO_DIR/test_bin" ]; then
    "$SCENARIO_DIR/test_bin"
elif [ -f "$PROJECT_ROOT/build/scenarios/06b_hub75_matrix_64x64/test_bin" ]; then
    "$PROJECT_ROOT/build/scenarios/06b_hub75_matrix_64x64/test_bin"
fi

echo "[Scenario 06b] PASSED!"
