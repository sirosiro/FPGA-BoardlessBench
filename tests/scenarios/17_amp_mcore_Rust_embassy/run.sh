#!/bin/bash
# F-BB: 17_amp_mcore_Rust_embassy 実行スクリプト
# オプション: --clean (または -c) で成果物を削除します。

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

if [ "$1" = "--clean" ] || [ "$1" = "-c" ]; then
    echo "[run.sh] Cleaning artifacts..."
    cd "$SCRIPT_DIR"
    cargo clean --manifest-path m_core/Cargo.toml
    rm -f test_bin mcore_embassy.elf m_core/Cargo.lock
    exit 0
fi

if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    
    echo "[run.sh] Compiling M-Core Embassy firmware via Cargo..."
    cargo build --manifest-path m_core/Cargo.toml --release -j 2 || exit 1
    cp m_core/target/release/mcore_embassy ./mcore_embassy.elf

    # 1. M-Core用ファームウェアを配置
    echo "[run.sh] Copying M-Core firmware to /lib/firmware/..."
    cp mcore_embassy.elf /lib/firmware/

    # 2. remoteprocにファイル名を指定して起動
    echo "[run.sh] Starting M-Core firmware via remoteproc..."
    echo mcore_embassy.elf > /sys/class/remoteproc/remoteproc0/firmware
    echo start > /sys/class/remoteproc/remoteproc0/state

    # 3. アプリを実行
    echo "[run.sh] Executing A-Core application..."
    ./test_bin
    RESULT=$?

    # 4. remoteprocの停止
    echo "[run.sh] Stopping M-Core firmware..."
    echo stop > /sys/class/remoteproc/remoteproc0/state
    
    # 5. 一時的に作成したバイナリのクリーンアップ
    rm -f mcore_embassy.elf
    
    exit $RESULT
else
    "$SCRIPT_DIR/../../scenario_runner.sh" "$SCRIPT_DIR" "$@"
fi
