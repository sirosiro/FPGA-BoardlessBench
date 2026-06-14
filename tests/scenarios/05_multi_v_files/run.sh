#!/bin/bash
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJECT_ROOT=$(cd "${SCRIPT_DIR}/../../.." && pwd)
if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    ./test_bin
else
    ${PROJECT_ROOT}/tests/scenario_runner.sh "$SCRIPT_DIR" "$@"
fi
