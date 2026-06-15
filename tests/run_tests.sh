#!/bin/bash

export LC_ALL=C
export LANG=C

# ==============================================================================
# F-BB Test Runner (Scenario-Based)
# ==============================================================================
# Usage:
#   ./tests/run_tests.sh           : Run all scenarios
#   ./tests/run_tests.sh --clean   : Clean build artifacts and logs
#   ./tests/run_tests.sh --interactive : Run tests and keep environment
# ==============================================================================

# --- Configuration ---
PROJECT_ROOT=$(pwd)
CONTROLLER="src/controller/vlogic_controller.py"
SIMULATOR="vfpga_sim"
SHIM="libfpgashim.so"
SCENARIOS_DIR="tests/scenarios"

# --- Functions ---
cleanup_processes() {
    echo -e "\n[Runner] Stopping background processes..."
    pkill -9 -f vlogic_controller || true
    pkill -9 -f vfpga_sim || true
    pkill -9 -f "dashboard/server.js" || true
    
    # remoteproc M-core processes cleanup
    if [ -f "/tmp/fbb/sys/class/remoteproc/remoteproc0/pid" ]; then
        MCORE_PID=$(cat /tmp/fbb/sys/class/remoteproc/remoteproc0/pid 2>/dev/null)
        if [ -n "$MCORE_PID" ]; then
            kill -9 $MCORE_PID 2>/dev/null
        fi
    fi
    rm -rf /tmp/fbb 2>/dev/null
}

# --- Argument Parsing ---
CLEAN=false
INTERACTIVE=false
CLEAN_TARGETS=""
export FORCE_MESA_FALLBACK=1

for arg in "$@"; do
    case $arg in
        --clean|-c) 
            CLEAN=true 
            if [[ ! " $CLEAN_TARGETS " =~ " clean " ]]; then
                CLEAN_TARGETS="$CLEAN_TARGETS clean"
            fi
            ;;
        --interactive|-i) 
            INTERACTIVE=true 
            ;;
        --*) 
            target=${arg#--}
            CLEAN=true
            CLEAN_TARGETS="$CLEAN_TARGETS $target"
            ;;
    esac
done

CLEAN_TARGETS=$(echo "$CLEAN_TARGETS" | xargs)

if [ "$CLEAN" = true ]; then
    if [ -z "$CLEAN_TARGETS" ]; then
        CLEAN_TARGETS="clean"
    fi
    echo "[Runner] Cleaning project artifacts and logs with targets: ${CLEAN_TARGETS}..."
    make clean
    rm -f tests/scenarios/*/test_bin tests/scenarios/*/*.bin
    for scenario in tests/scenarios/*; do
        if [ -f "${scenario}/Makefile" ]; then
            make -C "${scenario}" ${CLEAN_TARGETS} > /dev/null 2>&1 || true
        fi
    done
    rm -f tests/scenarios/*/*.log
    rm -f *.log
    rm -f board_manifest.json
    rm -f dashboard/data/*.json
    rm -f /tmp/hdmi_output.bmp
    echo "[Runner] Clean finished."
    exit 0
fi

trap cleanup_processes EXIT

start_environment() {
    local scenario_dir=$1
    local dts="${scenario_dir}/config.dts"
    mkdir -p /lib/firmware 2>/dev/null || true
    echo -e "\n[Runner] >>> SCENARIO: $(basename ${scenario_dir}) <<<"

    # 中間ファイルの削除
    rm -f board_manifest.json
    rm -f dashboard/data/*.json
    rm -f /tmp/hdmi_output.bmp

    # 前のシナリオの残骸を削除し、クリーンな状態にする
    make clean > /dev/null 2>&1

    echo "[Runner] Setting up environment with ${dts}..."
    
    # 1. Generate code from DTS
    python3 scripts/gen_vfpga.py ${dts}
    
    make libfpgashim.so || exit 1
    make engine SCENARIO_DIR="${scenario_dir}" || exit 1
    
    # 3. Start Controller
    python3 -u ${CONTROLLER} ${dts} > "${scenario_dir}/controller.log" 2>&1 &
    
    # 4. Start Simulator
    ./${SIMULATOR} > "${scenario_dir}/simulator.log" 2>&1 &
    
    sleep 3
}

# --- Main Execution ---

for scenario in ${SCENARIOS_DIR}/*; do
    if [ ! -d "${scenario}" ]; then continue; fi
    
    # Skip showcase scenarios starting with 'S'
    if [[ $(basename "${scenario}") == S* ]]; then
        echo "[Runner] Skipping showcase scenario: $(basename ${scenario})"
        continue
    fi
    
    start_environment "${scenario}"
    
    # Build the scenario via Makefile
    echo "[Runner] Building ${scenario} via Makefile..."
    make -C "${scenario}" || exit 1
    
    # Run the test
    echo "[Runner] Running test..."
    export LD_PRELOAD=$PWD/${SHIM}
    export FBB_ACTIVE=1
    ./${scenario}/run.sh
    RESULT=$?
    unset FBB_ACTIVE
    unset LD_PRELOAD
    
    if [ $RESULT -eq 0 ]; then
        echo "[Runner] RESULT: $(basename ${scenario}) PASSED"
    else
        echo "[Runner] RESULT: $(basename ${scenario}) FAILED"
        exit 1
    fi
    
    if [ "$INTERACTIVE" = false ]; then
        cleanup_processes
    else
        echo "[Runner] INTERACTIVE MODE: Environment is being maintained."
        echo "[Runner] Press Enter to continue to next scenario (or stop)..."
        read
        cleanup_processes
    fi
done

echo -e "\n[Runner] ALL SCENARIOS COMPLETED SUCCESSFULLY!"
