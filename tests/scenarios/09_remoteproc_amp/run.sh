#!/bin/bash
# F-BB: 09_remoteproc_amp 実行スクリプト
# このスクリプトを叩くだけで、このディレクトリの環境が立ち上がりテストが実行されます。
# オプション: --clean (または -c) で成果物を削除します。

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    # 1. R5用ファームウェアを配置
    cp mcore_baremetal.elf /lib/firmware/

    # 2. remoteprocにファイル名を指定して起動
    echo mcore_baremetal.elf > /sys/class/remoteproc/remoteproc0/firmware
    echo start > /sys/class/remoteproc/remoteproc0/state

    # 3. アプリを実行
    ./test_bin
else
    "$SCRIPT_DIR/../../scenario_runner.sh" "$SCRIPT_DIR" "$@"
fi
