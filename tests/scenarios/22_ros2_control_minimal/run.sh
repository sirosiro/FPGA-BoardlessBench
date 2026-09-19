#!/bin/bash
# F-BB: Scenario 22 ros2_control Minimal UIO Interface Test Runner

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    
    if [ -f "./test_bin" ]; then
        ./test_bin
    elif [ -f "../../build/scenarios/22_ros2_control_minimal/test_bin" ]; then
        ../../build/scenarios/22_ros2_control_minimal/test_bin
    else
        echo "[Scenario 22] Compiling test_bin via g++..."
        g++ -std=c++20 -O2 -g main.cpp uio_robot_hardware.cpp -I. -I./include -I../../src/include -lpthread -lrt -o test_bin
        ./test_bin
    fi
else
    "$SCRIPT_DIR/../../scenario_runner.sh" "$SCRIPT_DIR" "$@"
fi
