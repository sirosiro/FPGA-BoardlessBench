#!/bin/bash
# F-BB Scenario Runner for P01_frdmIMX

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# Check and auto-install dependencies (Mesa GLES/EGL headers)
if [ ! -f /usr/include/EGL/egl.h ]; then
    echo "[P01_frdmIMX] Mesa EGL headers not found. Attempting auto-installation..."
    if [ -w /etc/apt/sources.list ] && command -v apt-get &> /dev/null; then
        apt-get update && apt-get install -y libegl1-mesa-dev libgles2-mesa-dev
    else
        echo "Error: EGL/egl.h is missing. Please run the following command as root in the container:"
        echo "  apt update && apt install -y libegl1-mesa-dev libgles2-mesa-dev"
        exit 1
    fi
fi

# F-BBなどのディスプレイやGPUハードウェアがないシミュレータホスト環境において、
# OpenGL ES/EGLの初期化をソフトウェアレンダリング（Mesa llvmpipe）にフォールバックさせるため、
# 環境変数 FORCE_MESA_FALLBACK=1 をデフォルトで設定します。
if [ -z "$FORCE_MESA_FALLBACK" ]; then
    export FORCE_MESA_FALLBACK=1
fi

# HDMIの画面出力先をダッシュボードプレビュー用のファイルダンプにフォールバックさせるため、
# 環境変数 FORCE_HOST_DISPLAY=1 をデフォルトで設定します。
if [ -z "$FORCE_HOST_DISPLAY" ]; then
    export FORCE_HOST_DISPLAY=1
fi


# Determine SOC type (default is imx95)
SOC_TYPE="imx95"
if [ "$1" = "imx8mp" ]; then
    SOC_TYPE="imx8mp"
    shift
elif [ "$1" = "imx95" ]; then
    SOC_TYPE="imx95"
    shift
fi

echo "[P01_frdmIMX] Preparing config.dts for SOC: ${SOC_TYPE}"
cp "${SCRIPT_DIR}/${SOC_TYPE}_config.dts" "${SCRIPT_DIR}/config.dts"

if [ "$FBB_ACTIVE" = "1" ]; then
    cd "${SCRIPT_DIR}/../../.."
    ./tests/scenarios/P01_frdmIMX/test_bin
else
    cd ../../../
    ./start_lab.sh tests/scenarios/P01_frdmIMX/ "$@"
fi

