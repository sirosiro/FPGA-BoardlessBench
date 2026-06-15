#!/bin/bash

# F-BB Integrated Launcher
# Usage: ./start_lab.sh <scenario_dir>

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <scenario_dir>"
    exit 1
fi

SCENARIO_DIR=$1
DTS_PATH="${SCENARIO_DIR}/config.dts"

if [ ! -f "${DTS_PATH}" ]; then
    echo "[Error] DTS file not found: ${DTS_PATH}"
    exit 1
fi

# 以前の実行で設定された可能性があるLD_PRELOADを解除
unset LD_PRELOAD

# クリーンアップ関数
cleanup() {
    echo ""
    echo "Stopping background processes..."
    [ -n "$CONTROLLER_PID" ] && kill $CONTROLLER_PID 2>/dev/null
    [ -n "$SIM_PID" ] && kill $SIM_PID 2>/dev/null
    [ -n "$DASHBOARD_PID" ] && kill $DASHBOARD_PID 2>/dev/null
    pkill -f "node dashboard/server.js" 2>/dev/null
    pkill test_bin 2>/dev/null
    
    # remoteproc M-core processes cleanup
    if [ -f "/tmp/fbb/sys/class/remoteproc/remoteproc0/pid" ]; then
        MCORE_PID=$(cat /tmp/fbb/sys/class/remoteproc/remoteproc0/pid 2>/dev/null)
        if [ -n "$MCORE_PID" ]; then
            kill -9 $MCORE_PID 2>/dev/null
        fi
    fi
    rm -rf /tmp/fbb 2>/dev/null

    # 共有メモリファイルとマニフェストの削除
    echo "Cleaning up temporary files..."
    rm -f /tmp/vfpga_* 2>/dev/null
    rm -f /tmp/hdmi_output.bmp 2>/dev/null
    rm -f dashboard/data/board_manifest.json 2>/dev/null
    
    echo "Done."
    exit
}

trap cleanup SIGINT SIGTERM

echo "===================================================="
echo "   F-BB Integrated Launcher"
echo "   Scenario: ${SCENARIO_DIR}"
echo "===================================================="

# 1. 準備
echo "[0/3] Cleaning up previous state..."
rm -f dashboard/data/vfpga_uart_* 2>/dev/null
rm -f /tmp/hdmi_output.bmp 2>/dev/null
rm -rf /tmp/fbb 2>/dev/null
pkill -f "node dashboard/server.js" 2>/dev/null
make clean > /dev/null 2>&1
mkdir -p /lib/firmware 2>/dev/null || true

# ダッシュボードの依存関係とビルドチェック
if [ ! -d "dashboard/node_modules" ] || [ ! -d "dashboard/client/dist" ]; then
    if ! command -v npm &> /dev/null; then
        echo "[Warning] 'npm' command not found. Skipping dashboard initialization."
        echo "          Please ensure Node.js/npm is installed to use the Web Dashboard."
    else
        echo "[Dashboard] Installing dependencies or building frontend..."
        if [ ! -d "dashboard/node_modules" ]; then
            echo "[Dashboard] Installing backend dependencies..."
            npm --prefix dashboard install || echo "[Warning] Failed to install dashboard backend dependencies!"
        fi
        if [ ! -d "dashboard/client/dist" ]; then
            if [ ! -d "dashboard/client/node_modules" ]; then
                echo "[Dashboard] Installing frontend dependencies..."
                npm --prefix dashboard/client install || echo "[Warning] Failed to install dashboard frontend dependencies!"
            fi
            echo "[Dashboard] Building frontend..."
            npm --prefix dashboard/client run build || echo "[Warning] Failed to build dashboard frontend!"
        fi
    fi
fi

# 2. 生成とビルド
echo "[1/3] Generating code and building artifacts..."

# コード生成
python3 scripts/gen_vfpga.py "${DTS_PATH}" || exit 1

# シミュレータとShimのビルド
make engine SCENARIO_DIR="${SCENARIO_DIR}" || { echo "[Error] Simulator build failed!"; exit 1; }

# アプリケーションのビルド
make -C "${SCENARIO_DIR}" || { echo "[Error] App build failed!"; exit 1; }

# 3. 起動
echo "[2/3] Starting background processes..."

# ログディレクトリの準備
LOG_DIR="logs"
mkdir -p ${LOG_DIR}

# バックエンドプロセスの起動
python3 -u src/controller/vlogic_controller.py "${DTS_PATH}" > ${LOG_DIR}/controller.log 2>&1 &
CONTROLLER_PID=$!

# シミュレータの起動（波形出力 vfpga.vcd が有効化されている）
./vfpga_sim > ${LOG_DIR}/sim.log 2>&1 &
SIM_PID=$!

node dashboard/server.js > ${LOG_DIR}/dashboard.log 2>&1 &
DASHBOARD_PID=$!

# バックエンドの準備を待機（最大5秒）
# 生成されたヘッダファイルからSHMファイル名を取得する
SHM_FILE_EXPECTED=$(grep -oP '#define SHM_FILE "\\K[^"]+' src/include/vfpga_config.h 2>/dev/null || echo "/tmp/vfpga_reg")

MAX_RETRIES=5
RETRY_COUNT=0
while [ ! -f "$SHM_FILE_EXPECTED" ] && [ $RETRY_COUNT -lt $MAX_RETRIES ]; do
    sleep 1
    RETRY_COUNT=$((RETRY_COUNT+1))
done

echo "[3/3] All systems GO!"
echo "----------------------------------------------------"
echo "  Web Dashboard : http://localhost:8080"
echo "  UART Console  : localhost:2000 (if enabled)"
echo "----------------------------------------------------"
echo "Press Ctrl+C to stop the lab (after app exits)."

# FWアプリケーションの起動
echo ""
echo "   Starting firmware application..."
export VFPGA_INTERACTIVE=1
export FORCE_MESA_FALLBACK=1
export FORCE_HOST_DISPLAY=1
export LD_PRELOAD="$PWD/libfpgashim.so"
export FBB_ACTIVE=1
"${SCENARIO_DIR}/run.sh"
unset FBB_ACTIVE
unset LD_PRELOAD

# アプリ終了後もバックエンドの状態を監視し続ける
while true; do
    if ! kill -0 ${CONTROLLER_PID} 2>/dev/null || \
       ! kill -0 ${SIM_PID} 2>/dev/null || \
       ! kill -0 ${DASHBOARD_PID} 2>/dev/null; then
        echo "[Warning] One of the processes has stopped."
        cleanup
    fi
    sleep 1
done
