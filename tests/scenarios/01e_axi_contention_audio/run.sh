#!/bin/bash
# F-BB: 01e_axi_contention_audio 実行スクリプト
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    ./test_bin "$@"
else
    "$SCRIPT_DIR/../../scenario_runner.sh" "$SCRIPT_DIR" "$@"
fi

