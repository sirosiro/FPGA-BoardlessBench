#!/bin/bash
# F-BB Scenario Runner
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    ./test_bin
else
    cd ../../../
    ./start_lab.sh tests/scenarios/07_minimum_template/
fi
