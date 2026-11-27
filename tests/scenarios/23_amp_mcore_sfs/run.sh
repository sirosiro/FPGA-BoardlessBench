#!/bin/bash
# F-BB: 23_amp_mcore_sfs 実行スクリプト
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    mkdir -p /lib/firmware 2>/dev/null || true

    # 1. M-Core 用ファームウェアを配置
    if [ -f "mcore_sfs.elf" ]; then
        cp mcore_sfs.elf /lib/firmware/
    elif [ -f "../../build/scenarios/23_amp_mcore_sfs/mcore_sfs.elf" ]; then
        cp ../../build/scenarios/23_amp_mcore_sfs/mcore_sfs.elf /lib/firmware/
    else
        echo "[run.sh Error] mcore_sfs.elf not found!"
        exit 1
    fi

    # 2. remoteproc にファイル名を指定して起動
    echo mcore_sfs.elf > /sys/class/remoteproc/remoteproc0/firmware
    echo start > /sys/class/remoteproc/remoteproc0/state

    # 3. A-Core アプリケーションを実行
    ./test_bin
    RESULT=$?

    # 4. remoteproc を停止
    echo stop > /sys/class/remoteproc/remoteproc0/state
    exit $RESULT
else
    "$SCRIPT_DIR/../../scenario_runner.sh" "$SCRIPT_DIR" "$@"
fi
