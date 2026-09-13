#!/bin/bash

export LC_ALL=C
export LANG=C

# ==============================================================================
# F-BB Scenario Runner (Shared Infrastructure)
# ==============================================================================
# このスクリプトは、個別のテストシナリオを実行するための共通ロジックです。
# 1. DTSからのコード生成 2. シミュレーションエンジンのビルド 3. バックグラウンド起動
# 4. アプリケーションのコンパイルと実行 5. プロセスの自動クリーンアップ
# を行います。
# ==============================================================================

PROJECT_ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCENARIO_PATH=""
CLEAN=false
CLEAN_TARGETS=""

SCENARIO_ARGS=()

# --- 引数解析 ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean*|--distclean|-c) 
            CLEAN=true 
            target=${1#--}
            CLEAN_TARGETS="$CLEAN_TARGETS $target"
            shift
            ;;
        --chaos)
            export FBB_CHAOS_MODE=1
            shift
            ;;
        --seed=*)
            export FBB_CHAOS_MODE=1
            export FBB_CHAOS_SEED="${1#--seed=}"
            shift
            ;;
        --seed)
            export FBB_CHAOS_MODE=1
            export FBB_CHAOS_SEED="$2"
            shift 2
            ;;
        --chaos-rate=*)
            export FBB_CHAOS_RATE="${1#--chaos-rate=}"
            shift
            ;;
        --chaos-targets=*)
            export FBB_CHAOS_TARGETS="${1#--chaos-targets=}"
            shift
            ;;
        *) 
            if [ -d "$1" ]; then 
                SCENARIO_PATH="$1"
            else
                SCENARIO_ARGS+=("$1")
            fi 
            shift
            ;;
    esac
done

CLEAN_TARGETS=$(echo "$CLEAN_TARGETS" | xargs)

if [ -z "$SCENARIO_PATH" ] && [ "$CLEAN" = false ]; then
    echo "Usage: $0 <scenario_directory_path> [--clean|-c] [--chaos] [--seed=<seed>]"
    exit 1
fi

# シナリオの絶対パスを取得 (クリーンモードで引数が指定されていない場合はカレントディレクトリまたは推測)
if [ -n "$SCENARIO_PATH" ]; then
    SCENARIO_DIR=$(cd "$SCENARIO_PATH" && pwd)
    SCENARIO_NAME=$(basename "$SCENARIO_DIR")
fi

# --- クリーンアップモード ---
if [ "$CLEAN" = true ]; then
    if [ -z "$CLEAN_TARGETS" ]; then
        CLEAN_TARGETS="clean"
    fi
    echo "[Runner] Cleaning artifacts for scenario: ${SCENARIO_NAME} with targets: ${CLEAN_TARGETS}..."
    if [ -d "build" ]; then rm -rf build/* build/.[!.]* 2>/dev/null; fi
    rm -f libfpgashim.so vfpga_sim 2>/dev/null
    rm -rf logs obj_dir 2>/dev/null
    rm -f /dev/shm/spi_adc /dev/shm/fbb_* 2>/dev/null
    rm -f /tmp/vring0 /tmp/vfpga_reg /tmp/fbb_compatible /tmp/fbb_model /tmp/uio* /tmp/fbb_uart_* /tmp/vfpga_uart_* /tmp/fbb_spi_* /tmp/fbb_i2c_* /tmp/fbb_protocol_violations.log 2>/dev/null
    if [ -n "$SCENARIO_DIR" ]; then
        if [[ " $CLEAN_TARGETS " =~ " distclean " || " $CLEAN_TARGETS " =~ " cleanall " ]]; then
            rm -rf "${SCENARIO_DIR}/FreeRTOS-Kernel" "${SCENARIO_DIR}/threadx" "${SCENARIO_DIR}/CMSIS-FreeRTOS" "${SCENARIO_DIR}/stm32-mw-cmsis-rtos-tx" "${SCENARIO_DIR}/CMSIS_5" 2>/dev/null
        fi
        rm -f "${SCENARIO_DIR}/test_bin" "${SCENARIO_DIR}/"*.elf "${SCENARIO_DIR}/"*.bin "${SCENARIO_DIR}/"*.o "${SCENARIO_DIR}/"*.vcd
    fi
    if [ -n "$SCENARIO_DIR" ]; then
        rm -f "${SCENARIO_DIR}/"*.log
    fi
    rm -f "${PROJECT_ROOT}/controller.log" "${PROJECT_ROOT}/simulator.log" "${PROJECT_ROOT}/"*.vcd
    echo "[Runner] Clean finished."
    exit 0
fi

# --- 設定 ---
CONTROLLER="${PROJECT_ROOT}/src/controller/vlogic_controller.py"
SIMULATOR="${PROJECT_ROOT}/vfpga_sim"
SHIM="${PROJECT_ROOT}/libfpgashim.so"
DTS="${SCENARIO_DIR}/config.dts"

# --- プロセス掃除関数 ---
cleanup() {
    echo -e "\n[Runner] Stopping background processes..."
    pkill -f vlogic_controller || true
    pkill -f vfpga_sim || true
    
    # remoteproc M-core processes cleanup (supports multiple M-cores)
    for pid_file in /tmp/fbb/sys/class/remoteproc/*/pid; do
        if [ -f "$pid_file" ]; then
            MCORE_PID=$(cat "$pid_file" 2>/dev/null)
            if [ -n "$MCORE_PID" ]; then
                kill -9 $MCORE_PID 2>/dev/null
            fi
        fi
    done
    pkill -f "mcore_.*\.elf" 2>/dev/null || true
    rm -rf /tmp/fbb /tmp/fbb_can_* /dev/shm/fbb_can_* 2>/dev/null
    rm -f /tmp/vring0 /tmp/vfpga_reg /tmp/fbb_compatible /tmp/fbb_model /tmp/uio* /tmp/fbb_uart_* /tmp/vfpga_uart_* /tmp/fbb_spi_* /tmp/fbb_i2c_* 2>/dev/null
}

# 異常終了時や中断時（Ctrl+C）にプロセスを掃除するように設定
trap cleanup EXIT

# 過去の違反ログを削除して新規実行を開始
rm -f /tmp/fbb_protocol_violations.log 2>/dev/null

# --- 実行フェーズ ---

# 確実にプロジェクトルートから実行を開始する
cd "${PROJECT_ROOT}"

mkdir -p /lib/firmware 2>/dev/null || true

echo -e "\n[Runner] >>> Starting Scenario: ${SCENARIO_NAME} <<<"

# 1. DTSからコード生成
echo "[Runner] Generating code from ${DTS}..."
python3 "${PROJECT_ROOT}/scripts/gen_vfpga.py" "${DTS}"

# 2. エンジンのビルド
# 【重要】コントローラ起動時にDTSで定義された周辺デバイスデーモン (fbb_spi_adc等) を正常に
# 立ち上げるため、バックグラウンド起動前にプロジェクト全体 (周辺デバイスを含む) をビルド完了させておく。
# そうしないと、対向デーモン不在によるソケット接続待ちでシミュレータがデッドロックします。
echo "[Runner] Building simulation engine (this may take a few seconds)..."
cd "${PROJECT_ROOT}"
if [ -d "build" ]; then rm -rf build/* build/.[!.]* 2>/dev/null; fi
cmake -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DSCENARIO_DIR="${SCENARIO_DIR}" || exit 1
cmake --build build || exit 1

# 3. バックグラウンドプロセスの起動
echo "[Runner] Starting Backend Controller & RTL Simulator..."
python3 -u "${CONTROLLER}" "${DTS}" > "${SCENARIO_DIR}/controller.log" 2>&1 &
"${SIMULATOR}" > "${SCENARIO_DIR}/simulator.log" 2>&1 &

# ダッシュボードサーバーが未起動の場合は自動的にバックグラウンド起動
if ! pgrep -f "node dashboard/server.js" > /dev/null 2>&1; then
    echo "[Runner] Starting Web Dashboard Server (http://localhost:8080)..."
    mkdir -p "${PROJECT_ROOT}/logs"
    nohup node "${PROJECT_ROOT}/dashboard/server.js" > "${PROJECT_ROOT}/logs/dashboard.log" 2>&1 </dev/null &
fi

# 通信の準備が整うまで少し待機
sleep 2

# 4. アプリケーションのビルド
echo "[Runner] Building application via CMake..."
cmake --build build || exit 1

# 5. アプリケーションの実行 (LD_PRELOADを使用)
echo "[Runner] Executing application with LD_PRELOAD..."
cd "${SCENARIO_DIR}"
chmod +x ./run.sh

# Prepend ASan runtime if enabled to avoid LD_PRELOAD ordering warning/disablement
if [[ "$CMAKE_ARGS" == *"-DFBB_ENABLE_ASAN=ON"* ]] || [ "$FBB_ENABLE_ASAN" = "ON" ]; then
    ASAN_SO=$(gcc -print-file-name=libasan.so 2>/dev/null)
    if [ -f "$ASAN_SO" ]; then
        export LD_PRELOAD="$ASAN_SO:${SHIM}"
    else
        export LD_PRELOAD="${SHIM}"
    fi
else
    export LD_PRELOAD="${SHIM}"
fi

export LD_BIND_NOW=1
export FBB_ACTIVE=1
./run.sh "${SCENARIO_ARGS[@]}"
RESULT=$?
unset FBB_ACTIVE
unset LD_PRELOAD
unset LD_BIND_NOW

if [ $RESULT -eq 0 ]; then
    echo -e "\n[Runner] RESULT: SUCCESS"
else
    echo -e "\n[Runner] RESULT: FAILURE (Exit Code: $RESULT)"
    if [ "$FBB_CHAOS_MODE" = "1" ]; then
        CHAOS_SEED_SAVED=$(cat /tmp/fbb_chaos_seed.txt 2>/dev/null)
        echo -e "\033[1;33m[Runner] ======================================================================\033[0m"
        echo -e "\033[1;33m[Runner] [CHAOS FAILURE DETECTED]\033[0m"
        echo -e "\033[1;33m[Runner] To reproduce this exact failure sequence deterministically, run:\033[0m"
        echo -e "\033[1;33m[Runner]   ./run.sh --chaos --seed=${CHAOS_SEED_SAVED}\033[0m"
        echo -e "\033[1;33m[Runner] ======================================================================\033[0m"
    fi
    echo "[Runner] Check controller.log and simulator.log in the scenario directory for details."
fi

exit $RESULT
