#!/bin/bash
# F-BB: 01b_uio_irq_interrupt 実行スクリプト

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    
    if [ -f "./test_bin" ]; then
        ./test_bin
    elif [ -f "../../build/scenarios/01b_uio_irq_interrupt/test_bin" ]; then
        ../../build/scenarios/01b_uio_irq_interrupt/test_bin
    else
        gcc -O2 -g main.c -o test_bin
        ./test_bin
    fi
else
    "$SCRIPT_DIR/../../scenario_runner.sh" "$SCRIPT_DIR" "$@"
fi
