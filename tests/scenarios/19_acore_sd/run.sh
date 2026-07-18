#!/bin/bash
# F-BB: 19_acore_sd 実行スクリプト
# このスクリプトを叩くだけで、このディレクトリの環境が立ち上がりテストが実行されます。
# オプション: --clean (または -c) で成果物を削除します。

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    export FBB_SD_DIR="$SCRIPT_DIR/sd_card"
    ./test_bin
else
    "$SCRIPT_DIR/../../scenario_runner.sh" "$SCRIPT_DIR" "$@"
fi
