#!/bin/bash
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    if [ "$VFPGA_INTERACTIVE" = "1" ]; then
        ./test_bin
    else
        ./test_bin --batch
    fi
else
    ../../scenario_runner.sh "$SCRIPT_DIR" "$@"
fi
