#!/usr/bin/env bash
set -e

SCENARIO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCENARIO_DIR/../../.." && pwd)"

echo "=========================================================="
echo " Running Scenario 06c: HUB75 128x64 Daisy-Chain Matrix"
echo "=========================================================="

cd "$PROJECT_ROOT"
PYTHONPATH=scripts python3 scripts/gen_vfpga.py "$SCENARIO_DIR/config.dts"

cd "$PROJECT_ROOT/build"
cmake ..
make -j$(nproc)

# Launch hub75_matrix plugin binary in background mapped to /dev/shm/fbb_hub75_chain0 (128x64 = 24,576 bytes)
PLUGIN_BIN="${PROJECT_ROOT}/build/bin/fbb_hub75_matrix"
if [ -f "${PLUGIN_BIN}" ]; then
    echo "[Scenario 06c] Starting hub75_matrix plugin binary for /dev/shm/fbb_hub75_chain0 128x64..."
    "${PLUGIN_BIN}" /dev/shm/fbb_hub75_chain0 128 64 &
    PLUGIN_PID=$!
    trap "kill -9 ${PLUGIN_PID} 2>/dev/null || true" EXIT
fi

sleep 1

# Execute firmware test binary
echo "[Scenario 06c] Executing test_bin..."
if [ -f "$SCENARIO_DIR/test_bin" ]; then
    "$SCENARIO_DIR/test_bin"
elif [ -f "$PROJECT_ROOT/build/scenarios/06c_hub75_matrix_daisy_chain/test_bin" ]; then
    "$PROJECT_ROOT/build/scenarios/06c_hub75_matrix_daisy_chain/test_bin"
fi

echo "=========================================================="
echo " Scenario 06c PASSED!"
echo "=========================================================="
