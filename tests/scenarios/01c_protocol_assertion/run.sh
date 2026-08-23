#!/bin/bash
# F-BB: 01c_protocol_assertion Execution Script
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    if [ -f "./test_bin" ]; then
        ./test_bin
    elif [ -f "$SCRIPT_DIR/../../build/scenarios/01c_protocol_assertion/test_bin" ]; then
        "$SCRIPT_DIR/../../build/scenarios/01c_protocol_assertion/test_bin"
    fi
else
    "$SCRIPT_DIR/../../scenario_runner.sh" "$SCRIPT_DIR" "$@"
fi
