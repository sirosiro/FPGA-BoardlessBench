#!/bin/bash
# F-BB: S01_cpp_lfsr_sequencer 実行スクリプト
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    ./test_bin
else
    "$SCRIPT_DIR/../../scenario_runner.sh" "$SCRIPT_DIR" "$@"
fi
