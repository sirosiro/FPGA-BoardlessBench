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
    rm -f /tmp/vring0 /tmp/vfpga_reg /tmp/fbb_compatible /tmp/fbb_model 2>/dev/null
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
    echo "[Runner] Cleaning project artifacts and logs..."
    if [ -d "build" ]; then rm -rf build/* build/.[!.]* 2>/dev/null; fi
    rm -f libfpgashim.so vfpga_sim
    rm -f tests/scenarios/*/test_bin tests/scenarios/*/*.bin tests/scenarios/*/*.elf
    
    if [[ " $CLEAN_TARGETS " =~ " distclean " || " $CLEAN_TARGETS " =~ " cleanall " ]]; then
        echo "[Runner] Performing distclean: removing FreeRTOS-Kernel..."
        rm -rf tests/scenarios/10_amp_mcore_freertos/FreeRTOS-Kernel 2>/dev/null
        echo "[Runner] Performing distclean: removing ThreadX source..."
        rm -rf tests/scenarios/11_amp_mcore_threadx/threadx 2>/dev/null
        echo "[Runner] Performing distclean: removing CMSIS-RTOS2 scenario external sources..."
        rm -rf tests/scenarios/12_amp_mcore_cmsis-rtos2-freertos/FreeRTOS-Kernel 2>/dev/null
        rm -rf tests/scenarios/12_amp_mcore_cmsis-rtos2-freertos/CMSIS-FreeRTOS 2>/dev/null
        rm -rf tests/scenarios/12_amp_mcore_cmsis-rtos2-freertos/CMSIS_5 2>/dev/null
        rm -rf tests/scenarios/13_amp_mcore_cmsis-rtos2-threadx/threadx 2>/dev/null
        rm -rf tests/scenarios/13_amp_mcore_cmsis-rtos2-threadx/stm32-mw-cmsis-rtos-tx 2>/dev/null
        rm -rf tests/scenarios/13_amp_mcore_cmsis-rtos2-threadx/CMSIS_5 2>/dev/null
        rm -rf tests/scenarios/15_amp_mcore_OpenAMP_freertos/FreeRTOS-Kernel 2>/dev/null
    fi

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
    if [ -d "build" ]; then rm -rf build/* build/.[!.]* 2>/dev/null; fi
    sync
    sleep 1
    rm -f libfpgashim.so vfpga_sim
    rm -f /tmp/vring0 /tmp/vfpga_reg /tmp/fbb_compatible /tmp/fbb_model 2>/dev/null

    echo "[Runner] Setting up environment with ${dts}..."
    
    # 1. Generate code from DTS
    python3 scripts/gen_vfpga.py ${dts}
    
    cmake -B build -DSCENARIO_DIR="${scenario_dir}" || exit 1
    cmake --build build --target fpgashim || exit 1
    cmake --build build --target vfpga_sim || exit 1
    
    # 3. Start Controller
    python3 -u ${CONTROLLER} ${dts} > "${scenario_dir}/controller.log" 2>&1 &
    
    # 4. Start Simulator
    ./${SIMULATOR} > "${scenario_dir}/simulator.log" 2>&1 &
    
    sleep 3
}

# --- Main Execution ---

for scenario in ${SCENARIOS_DIR}/13_amp_mcore_cmsis-rtos2-threadx ${SCENARIOS_DIR}/14_amp_mcore_OpenAMP_baremetal ${SCENARIOS_DIR}/15_amp_mcore_OpenAMP_freertos; do
    if [ ! -d "${scenario}" ]; then continue; fi
    
    # Skip showcase scenarios starting with 'S'
    if [[ $(basename "${scenario}") == S* ]]; then
        echo "[Runner] Skipping showcase scenario: $(basename ${scenario})"
        continue
    fi
    
    start_environment "${scenario}"
    
    # Build the scenario via CMake
    echo "[Runner] Building ${scenario} via CMake..."
    cmake --build build || exit 1
    
    # Run the test
    echo "[Runner] Running test..."
    chmod +x ./${scenario}/run.sh
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
