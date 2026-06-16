#!/bin/bash
# F-BB: 12_amp_mcore_cmsis-rtos2-freertos 実行スクリプト
# オプション: --clean (または -c) で成果物を削除します。

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    # 1. M-Core用ファームウェアを配置
    cp mcore_rtos.elf /lib/firmware/

    # 2. remoteprocにファイル名を指定して起動
    echo mcore_rtos.elf > /sys/class/remoteproc/remoteproc0/firmware
    echo start > /sys/class/remoteproc/remoteproc0/state

    # 3. アプリを実行
    ./test_bin
else
    "$SCRIPT_DIR/../../scenario_runner.sh" "$SCRIPT_DIR" "$@"
fi
