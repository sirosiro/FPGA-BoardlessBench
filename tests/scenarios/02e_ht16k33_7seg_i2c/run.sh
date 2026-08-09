#!/bin/bash
# F-BB: 02e_ht16k33_7seg_i2c 実行スクリプト
# このスクリプトを叩くだけで、このディレクトリの環境が立ち上がりテストが実行されます。

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    ./test_bin
else
    "$SCRIPT_DIR/../../scenario_runner.sh" "$SCRIPT_DIR" "$@"
fi
