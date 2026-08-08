#!/bin/bash
# F-BB: 20b_mcore_cdma Scenario Runner Script

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    
    # 1. Deploy M-Core Firmware
    echo "[run.sh] Deploying M-Core CDMA firmware to /lib/firmware/..."
    cp mcore_cdma.elf /lib/firmware/ 2>/dev/null || true

    # 2. Start M-Core via simulated remoteproc interface
    echo "[run.sh] Starting M-Core CDMA firmware via RemoteProc..."
    echo mcore_cdma.elf > /sys/class/remoteproc/remoteproc0/firmware 2>/dev/null || true
    echo start > /sys/class/remoteproc/remoteproc0/state 2>/dev/null || true

    sleep 1

    if [ "$VFPGA_INTERACTIVE" = "1" ]; then
        echo "[run.sh] Starting A-Core Host Controller in Interactive Mode..."
        ./test_bin
        RESULT=$?
    else
        # 3. Execute M-Core Firmware Batch Regression Mode
        echo "[run.sh] Executing M-Core CDMA Firmware Batch Regression Test..."
        ./mcore_cdma.elf --batch
        RESULT=$?
    fi

    # 4. Stop M-Core via simulated remoteproc interface
    echo stop > /sys/class/remoteproc/remoteproc0/state 2>/dev/null || true

    exit $RESULT
else
    "$SCRIPT_DIR/../../scenario_runner.sh" "$SCRIPT_DIR" "$@"
fi
