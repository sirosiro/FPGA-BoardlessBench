#!/bin/bash
# F-BB: 14_amp_mcore_OpenAMP_baremetal 実行スクリプト

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    
    # 1. Deploy M-Core Firmware
    echo "[run.sh] Copying M-Core firmware to /lib/firmware/..."
    cp mcore_baremetal.elf /lib/firmware/

    # 2. Start M-Core via simulated remoteproc interface
    echo "[run.sh] Starting M-Core firmware..."
    echo mcore_baremetal.elf > /sys/class/remoteproc/remoteproc0/firmware
    echo start > /sys/class/remoteproc/remoteproc0/state

    # Allow endpoints to synchronize
    sleep 2

    # 3. Run A-Core Test Application
    echo "[run.sh] Executing test_bin..."
    ./test_bin
    RESULT=$?

    # 4. Stop M-Core via simulated remoteproc interface
    echo "[run.sh] Stopping M-Core firmware..."
    echo stop > /sys/class/remoteproc/remoteproc0/state
    sleep 1

    exit $RESULT
else
    "$SCRIPT_DIR/../../scenario_runner.sh" "$SCRIPT_DIR" "$@"
fi
