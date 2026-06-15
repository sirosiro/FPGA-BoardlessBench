#!/bin/bash

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

# --- 引数解析 ---
for arg in "$@"; do
    case $arg in
        --clean|-c) 
            CLEAN=true 
            if [[ ! " $CLEAN_TARGETS " =~ " clean " ]]; then
                CLEAN_TARGETS="$CLEAN_TARGETS clean"
            fi
            ;;
        --*) 
            target=${arg#--}
            CLEAN=true
            CLEAN_TARGETS="$CLEAN_TARGETS $target"
            ;;
        *) 
            if [ -d "$arg" ]; then 
                SCENARIO_PATH="$arg"; 
            fi 
            ;;
    esac
done

CLEAN_TARGETS=$(echo "$CLEAN_TARGETS" | xargs)

if [ -z "$SCENARIO_PATH" ] && [ "$CLEAN" = false ]; then
    echo "Usage: $0 <scenario_directory_path> [--clean|-c]"
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
    cd "${PROJECT_ROOT}"
    rm -rf build 2>/dev/null
    rm -f libfpgashim.so vfpga_sim
    if [ -n "$SCENARIO_DIR" ]; then
        if [[ " $CLEAN_TARGETS " =~ " distclean " || " $CLEAN_TARGETS " =~ " cleanall " ]]; then
            rm -rf "${SCENARIO_DIR}/FreeRTOS-Kernel" "${SCENARIO_DIR}/threadx" 2>/dev/null
        fi
        rm -f "${SCENARIO_DIR}/test_bin" "${SCENARIO_DIR}/"*.elf "${SCENARIO_DIR}/"*.bin
    fi
    if [ -n "$SCENARIO_DIR" ]; then
        rm -f "${SCENARIO_DIR}/"*.log
    fi
    rm -f "${PROJECT_ROOT}/controller.log" "${PROJECT_ROOT}/simulator.log"
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
    pkill -f "node dashboard/server.js" || true
    
    # remoteproc M-core processes cleanup
    if [ -f "/tmp/fbb/sys/class/remoteproc/remoteproc0/pid" ]; then
        MCORE_PID=$(cat /tmp/fbb/sys/class/remoteproc/remoteproc0/pid 2>/dev/null)
        if [ -n "$MCORE_PID" ]; then
            kill -9 $MCORE_PID 2>/dev/null
        fi
    fi
    rm -rf /tmp/fbb 2>/dev/null
}

# 異常終了時や中断時（Ctrl+C）にプロセスを掃除するように設定
trap cleanup EXIT

# --- 実行フェーズ ---

# 確実にプロジェクトルートから実行を開始する
cd "${PROJECT_ROOT}"

mkdir -p /lib/firmware 2>/dev/null || true

echo -e "\n[Runner] >>> Starting Scenario: ${SCENARIO_NAME} <<<"

# 1. DTSからコード生成
echo "[Runner] Generating code from ${DTS}..."
python3 "${PROJECT_ROOT}/scripts/gen_vfpga.py" "${DTS}"

# 3. エンジンのビルド
echo "[Runner] Building simulation engine (this may take a few seconds)..."
cd "${PROJECT_ROOT}"
cmake -B build -DSCENARIO_DIR="${SCENARIO_DIR}" || exit 1
cmake --build build --target fpgashim --target vfpga_sim || exit 1

# 4. バックグラウンドプロセスの起動
echo "[Runner] Starting Backend Controller & RTL Simulator..."
python3 -u "${CONTROLLER}" "${DTS}" > "${SCENARIO_DIR}/controller.log" 2>&1 &
"${SIMULATOR}" > "${SCENARIO_DIR}/simulator.log" 2>&1 &

# 通信の準備が整うまで少し待機
sleep 2

# 5. アプリケーションのビルド
echo "[Runner] Building application via CMake..."
cmake --build build || exit 1

# 6. アプリケーションの実行 (LD_PRELOADを使用)
echo "[Runner] Executing application with LD_PRELOAD..."
cd "${SCENARIO_DIR}"
export LD_PRELOAD="${SHIM}"
export FBB_ACTIVE=1
./run.sh
RESULT=$?
unset FBB_ACTIVE
unset LD_PRELOAD

if [ $RESULT -eq 0 ]; then
    echo -e "\n[Runner] RESULT: SUCCESS"
else
    echo -e "\n[Runner] RESULT: FAILURE (Exit Code: $RESULT)"
    echo "[Runner] Check controller.log and simulator.log in the scenario directory for details."
fi

exit $RESULT
