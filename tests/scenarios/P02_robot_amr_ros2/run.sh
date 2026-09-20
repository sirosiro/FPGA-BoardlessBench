#!/bin/bash
# F-BB: Scenario P02 Autonomous Mobile Robot (AMR) ros2_control Runner

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "$FBB_ACTIVE" = "1" ]; then
    cd "$SCRIPT_DIR"
    
    if [ -f "./test_bin" ]; then
        ./test_bin "$@"
    elif [ -f "../../build/scenarios/P02_robot_amr_ros2/test_bin" ]; then
        ../../build/scenarios/P02_robot_amr_ros2/test_bin "$@"
    else
        echo "[Scenario P02] Compiling test_bin via g++..."
        g++ -std=c++20 -O2 -g main.cpp uio_robot_hardware.cpp amr_kinematics.cpp -I. -I./include -I../../src/include -lpthread -lrt -o test_bin
        ./test_bin "$@"
    fi
else
    "$SCRIPT_DIR/../../scenario_runner.sh" "$SCRIPT_DIR" "$@"
fi
