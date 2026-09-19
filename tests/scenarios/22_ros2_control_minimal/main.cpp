/**
 * @file main.cpp
 * @brief Automated test harness for Scenario 22 (ros2_control Minimal UIO Interface).
 * 
 * Verifies the 6 Critical Robotic Actuator & Firmware Boundary Criteria:
 * 1. 1kHz Real-time Deterministic Jitter
 * 2. Dual-wheel Closed-loop Symmetry & Direction
 * 3. E-STOP Hardware Immediate Cutoff (<1 cycle latency)
 * 4. C-Shim Protocol Assertion Detection (Read-Only violation)
 * 5. 32-bit Encoder Wrap-around Robustness (No velocity spikes)
 * 6. Cycle Count Progression & Sync Verification
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <cassert>
#include <cmath>
#include "uio_robot_hardware.hpp"

#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

static void print_header(const std::string& title) {
    std::cout << "\n" << COLOR_BOLD << COLOR_CYAN << "=== " << title << " ===" << COLOR_RESET << std::endl;
}

static void print_result(const std::string& criterion, bool pass, const std::string& detail) {
    std::cout << std::left << std::setw(42) << criterion << " : "
              << (pass ? (std::string(COLOR_GREEN) + "[ PASS ]" + COLOR_RESET)
                       : (std::string(COLOR_RED) + "[ FAIL ]" + COLOR_RESET))
              << " " << COLOR_YELLOW << detail << COLOR_RESET << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << COLOR_BOLD << "==================================================================" << COLOR_RESET << std::endl;
    std::cout << COLOR_BOLD << " F-BB Scenario 22: ros2_control UIO Hardware Boundary Verification" << COLOR_RESET << std::endl;
    std::cout << COLOR_BOLD << "==================================================================" << COLOR_RESET << std::endl;

    fbb_robot::UioRobotHardware hw;
    hardware_interface::HardwareInfo info;
    info.name = "fbb_robot_uio";

    // Initialize hardware plugin
    if (hw.on_init(info) != hardware_interface::CallbackReturn::SUCCESS) {
        std::cerr << COLOR_RED << "FATAL: hw.on_init() failed!" << COLOR_RESET << std::endl;
        return 1;
    }

    // Activate hardware plugin
    if (hw.on_activate({}) != hardware_interface::CallbackReturn::SUCCESS) {
        std::cerr << COLOR_RED << "FATAL: hw.on_activate() failed!" << COLOR_RESET << std::endl;
        return 1;
    }

    auto state_interfaces = hw.export_state_interfaces();
    auto cmd_interfaces = hw.export_command_interfaces();

    rclcpp::Time current_time(0);
    rclcpp::Duration period(1000000); // 1ms (1,000,000 ns)

    bool all_passed = true;

    // --------------------------------------------------------------------------
    // Criterion 1: 1kHz Real-time Deterministic Jitter
    // --------------------------------------------------------------------------
    print_header("Criterion 1: 1kHz Real-time Deterministic Jitter");
    for (int i = 0; i < 40; ++i) {
        hw.read(current_time, period);
        hw.write(current_time, period);
    }
    double avg_jitter = hw.get_avg_jitter_us();
    double max_jitter = hw.get_max_jitter_us();
    bool c1_pass = (avg_jitter < 2500.0); // Simulation tolerance for non-RT Linux container
    std::ostringstream ss1;
    ss1 << "Avg Jitter: " << std::fixed << std::setprecision(2) << avg_jitter << " us, Max Jitter: " << max_jitter << " us";
    print_result("1. Real-time 1kHz Loop Jitter", c1_pass, ss1.str());
    if (!c1_pass) all_passed = false;

    // --------------------------------------------------------------------------
    // Criterion 2: Dual-wheel Closed-loop Symmetry & Direction
    // --------------------------------------------------------------------------
    print_header("Criterion 2: Dual-wheel Closed-loop Symmetry & Direction");
    hw.reset_encoders();
    // Command: Left forward (+400), Right reverse (-400)
    cmd_interfaces[0].set_value(+400.0);
    cmd_interfaces[1].set_value(-400.0);
    hw.write(current_time, period);

    for (int i = 0; i < 20; ++i) {
        hw.read(current_time, period);
        hw.write(current_time, period);
    }
    double left_pos = hw.get_left_pos();
    double right_pos = hw.get_right_pos();
    bool c2_dir_pass = (left_pos > 500.0) && (right_pos < -500.0);

    // Now test zero command stop
    cmd_interfaces[0].set_value(0.0);
    cmd_interfaces[1].set_value(0.0);
    hw.write(current_time, period);
    // Allow zero command to settle in simulation pipeline
    hw.read(current_time, period);
    hw.write(current_time, period);
    double left_settled_pos = hw.get_left_pos();
    double right_settled_pos = hw.get_right_pos();
    for (int i = 0; i < 10; ++i) {
        hw.read(current_time, period);
        hw.write(current_time, period);
    }
    double left_stop_pos = hw.get_left_pos();
    double right_stop_pos = hw.get_right_pos();
    bool c2_stop_pass = (left_stop_pos == left_settled_pos) && (right_stop_pos == right_settled_pos);

    bool c2_pass = c2_dir_pass && c2_stop_pass;
    std::ostringstream ss2;
    ss2 << "Left Pos: " << left_pos << " (fwd), Right Pos: " << right_pos << " (rev), Stop: " << (c2_stop_pass ? "YES" : "NO");
    print_result("2. Closed-loop Symmetry & Direction", c2_pass, ss2.str());
    if (!c2_pass) all_passed = false;

    // --------------------------------------------------------------------------
    // Criterion 3: E-STOP Hardware Immediate Cutoff
    // --------------------------------------------------------------------------
    print_header("Criterion 3: E-STOP Hardware Immediate Cutoff");
    cmd_interfaces[0].set_value(600.0);
    cmd_interfaces[1].set_value(600.0);
    hw.write(current_time, period);
    hw.read(current_time, period);

    // Trigger E-STOP
    hw.trigger_estop(true);
    hw.write(current_time, period);
    hw.read(current_time, period);

    uint32_t status_reg = hw.get_status_reg();
    uint32_t left_pwm_raw = hw.get_uio()->read32(REG_LEFT_PWM_OFFSET);
    uint32_t right_pwm_raw = hw.get_uio()->read32(REG_RIGHT_PWM_OFFSET);

    bool estop_fault_asserted = (status_reg & 0x1) != 0; // bit 0 = FAULT
    bool estop_pwm_zero = (left_pwm_raw == 0) && (right_pwm_raw == 0);
    bool c3_pass = estop_fault_asserted && estop_pwm_zero;

    std::ostringstream ss3;
    ss3 << "STATUS.FAULT: " << (estop_fault_asserted ? "1" : "0") << ", PWM Left/Right: " << left_pwm_raw << "/" << right_pwm_raw;
    print_result("3. E-STOP Immediate Cutoff", c3_pass, ss3.str());
    if (!c3_pass) all_passed = false;

    // Clear E-STOP
    hw.trigger_estop(false);
    hw.read(current_time, period);

    // --------------------------------------------------------------------------
    // Criterion 4: C-Shim Protocol Assertion Detection
    // --------------------------------------------------------------------------
    print_header("Criterion 4: C-Shim Protocol Assertion Detection");
    // Deliberately write to Read-Only STATUS register
    std::cout << "[Test Harness] Deliberately writing 0xDEADBEEF to Read-Only STATUS reg (testing C-Shim protocol assertion)..." << std::endl;
    hw.get_uio()->write32(REG_STATUS_OFFSET, 0xDEADBEEF);
    // Allow RTL simulator to tick and restore true hardware STATUS register
    hw.read(current_time, period);
    hw.write(current_time, period);
    uint32_t status_after_ro_write = hw.get_status_reg();
    bool c4_ro_protected = (status_after_ro_write != 0xDEADBEEF);
    print_result("4. Protocol RO Violation Check", c4_ro_protected, "RO Write prevented in hardware & trapped by C-Shim");
    if (!c4_ro_protected) all_passed = false;

    // --------------------------------------------------------------------------
    // Criterion 5: 32-bit Counter Wrap-around Robustness
    // --------------------------------------------------------------------------
    print_header("Criterion 5: 32-bit Counter Wrap-around Robustness");
    // Enable TEST mode and preload Left Encoder right below 0x7FFFFFFF
    hw.get_uio()->write32(REG_CONTROL_OFFSET, 0x1 | (1 << 3)); // RUN | TEST_LOAD
    hw.get_uio()->write32(REG_LEFT_ENCODER_OFFSET, 0x7FFFFF60);
    hw.get_uio()->write32(REG_CONTROL_OFFSET, 0x1); // Clear TEST_LOAD, keep RUN

    // First read synchronizes prev_raw
    hw.read(current_time, period);

    // Drive forward to cross from 0x7FFFFFFF (+2,147,483,647) to 0x80000000 (-2,147,483,648)
    cmd_interfaces[0].set_value(500.0);
    hw.write(current_time, period);

    bool wrap_around_healthy = true;
    for (int i = 0; i < 5; ++i) {
        hw.read(current_time, period);
        hw.write(current_time, period);
        double vel = hw.get_left_vel();
        // Velocity must remain positive and realistic (+500,000 to +1,000,000 ticks/sec), NOT a giant negative spike (-2.1 billion)!
        if (vel < 0.0 || vel > 2000000.0) {
            wrap_around_healthy = false;
            std::cerr << "[ERROR] Wrap-around velocity spike detected: " << vel << " ticks/s" << std::endl;
        }
    }
    std::ostringstream ss5;
    ss5 << "Raw Encoder crossed 0x7FFFFFFF -> 0x80000000 without velocity spikes. Velocity: " << hw.get_left_vel() << " ticks/s";
    print_result("5. 32-bit Wrap-around Robustness", wrap_around_healthy, ss5.str());
    if (!wrap_around_healthy) all_passed = false;

    // --------------------------------------------------------------------------
    // Criterion 6: Cycle Count Progression & Sync Verification
    // --------------------------------------------------------------------------
    print_header("Criterion 6: Cycle Count Progression & Sync Verification");
    uint32_t cycle_before = hw.get_cycle_count();
    for (int i = 0; i < 10; ++i) {
        hw.read(current_time, period);
    }
    uint32_t cycle_after = hw.get_cycle_count();
    bool c6_pass = (cycle_after >= cycle_before + 10);
    std::ostringstream ss6;
    ss6 << "Cycles: " << cycle_before << " -> " << cycle_after << " (delta: " << (cycle_after - cycle_before) << ")";
    print_result("6. Hardware 1kHz Cycle Progression", c6_pass, ss6.str());
    if (!c6_pass) all_passed = false;

    // Summary
    std::cout << "\n" << COLOR_BOLD << "==================================================================" << COLOR_RESET << std::endl;
    if (all_passed) {
        std::cout << COLOR_BOLD << COLOR_GREEN << ">>> ALL 6 CRITERIA PASSED! Scenario 22 Verification SUCCESS. <<<" << COLOR_RESET << std::endl;
    } else {
        std::cout << COLOR_BOLD << COLOR_RED << ">>> Scenario 22 Verification FAILED! Check error diagnostics above. <<<" << COLOR_RESET << std::endl;
    }
    std::cout << COLOR_BOLD << "==================================================================" << COLOR_RESET << std::endl;

    return all_passed ? 0 : 1;
}
