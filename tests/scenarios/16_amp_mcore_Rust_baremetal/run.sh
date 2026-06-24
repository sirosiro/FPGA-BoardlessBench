#!/bin/bash
# F-BB: 16_amp_mcore_Rust_baremetal 実行スクリプト
# オプション: --clean (または -c) で成果物を削除します。

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    
    echo "[run.sh] Compiling Rust M-Core firmware with host BSP..."
    rustc --crate-type=lib --emit=obj -C panic=abort mcore.rs -o mcore.o || exit 1
    rustc -C panic=abort -C link-arg=mcore.o host_bsp.rs -l c -o mcore_rust.elf || exit 1
    rm -f mcore.o

    # 1. M-Core用ファームウェアを配置
    echo "[run.sh] Copying M-Core firmware to /lib/firmware/..."
    cp mcore_rust.elf /lib/firmware/

    # 2. remoteprocにファイル名を指定して起動
    echo "[run.sh] Starting M-Core firmware via remoteproc..."
    echo mcore_rust.elf > /sys/class/remoteproc/remoteproc0/firmware
    echo start > /sys/class/remoteproc/remoteproc0/state

    # 3. アプリを実行
    echo "[run.sh] Executing A-Core application..."
    ./test_bin
    RESULT=$?

    # 4. remoteprocの停止
    echo "[run.sh] Stopping M-Core firmware..."
    echo stop > /sys/class/remoteproc/remoteproc0/state
    
    # 5. 一時的に作成したバイナリのクリーンアップ
    rm -f mcore_rust.elf
    
    exit $RESULT
else
    "$SCRIPT_DIR/../../scenario_runner.sh" "$SCRIPT_DIR" "$@"
fi
