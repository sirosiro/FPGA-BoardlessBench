#!/bin/bash
# F-BB: 04b_dev_mem_violation_legacy 実行スクリプト
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    rm -f /tmp/fbb_memory_violation
    ./test_bin
    CODE=$?
    if [ -f /tmp/fbb_memory_violation ]; then
        echo -e "\n[Scenario 04b] SUCCESS: Memory Guard correctly intercepted out-of-bounds MMIO access."
        exit 0
    else
        echo -e "\n[Scenario 04b] FAILED: Memory Guard failed to intercept out-of-bounds access."
        exit $CODE
    fi
else
    "$SCRIPT_DIR/../../scenario_runner.sh" "$SCRIPT_DIR" "$@"
fi
