/**
 * @file uio_robot_hardware.cpp
 * @brief Implementation of UioRobotHardware for Scenario P02 AMR.
 */

#include "uio_robot_hardware.hpp"
#include <iostream>

namespace fbb_robot {

UioRobotHardware::~UioRobotHardware() {
    if (uio_dev_) {
        // Stop timer and motors on exit
        uio_dev_->write32(REG_CONTROL_OFFSET, 0);
    }
}

hardware_interface::CallbackReturn UioRobotHardware::on_init(const hardware_interface::HardwareInfo & /*info*/) {
    try {
        uio_dev_ = std::make_unique<UioDevice>(UIO_DEV_PATH, MMIO_REGION_SIZE);
    } catch (const std::exception& e) {
        std::cerr << "[UioRobotHardware] Initialization failed: " << e.what() << std::endl;
        return hardware_interface::CallbackReturn::ERROR;
    }

    // Initialize registers
    uio_dev_->write32(REG_CONTROL_OFFSET, 0x0);
    uio_dev_->write32(REG_LEFT_PWM_OFFSET, 0x0);
    uio_dev_->write32(REG_RIGHT_PWM_OFFSET, 0x0);

    return hardware_interface::CallbackReturn::SUCCESS;
}

std::vector<hardware_interface::StateInterface> UioRobotHardware::export_state_interfaces() {
    std::vector<hardware_interface::StateInterface> state_interfaces;
    state_interfaces.emplace_back("joint_left", "position", &left_pos_);
    state_interfaces.emplace_back("joint_left", "velocity", &left_vel_);
    state_interfaces.emplace_back("joint_right", "position", &right_pos_);
    state_interfaces.emplace_back("joint_right", "velocity", &right_vel_);
    return state_interfaces;
}

std::vector<hardware_interface::CommandInterface> UioRobotHardware::export_command_interfaces() {
    std::vector<hardware_interface::CommandInterface> command_interfaces;
    command_interfaces.emplace_back("joint_left", "velocity", &left_cmd_);
    command_interfaces.emplace_back("joint_right", "velocity", &right_cmd_);
    return command_interfaces;
}

hardware_interface::CallbackReturn UioRobotHardware::on_activate(const rclcpp_lifecycle::State & /*previous_state*/) {
    if (!uio_dev_) return hardware_interface::CallbackReturn::ERROR;

    // Unmask UIO Interrupt
    uint32_t unmask = 1;
    if (::write(uio_dev_->get_fd(), &unmask, sizeof(unmask)) != sizeof(unmask)) {
        std::cerr << "[UioRobotHardware] Failed to unmask UIO interrupt." << std::endl;
        return hardware_interface::CallbackReturn::ERROR;
    }

    // Enable RUN mode (bit 0 = 1)
    uio_dev_->write32(REG_CONTROL_OFFSET, 0x1);
    first_read_ = true;

    return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::CallbackReturn UioRobotHardware::on_deactivate(const rclcpp_lifecycle::State & /*previous_state*/) {
    if (uio_dev_) {
        // Stop timer
        uio_dev_->write32(REG_CONTROL_OFFSET, 0x0);
    }
    return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::return_type UioRobotHardware::read(const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/) {
    if (!uio_dev_) return hardware_interface::return_type::ERROR;

    // 1. Block on read() until 1kHz IRQ arrives from FPGA
    uint32_t irq_count = 0;
    ssize_t n = ::read(uio_dev_->get_fd(), &irq_count, sizeof(irq_count));
    if (n != sizeof(irq_count)) {
        return hardware_interface::return_type::ERROR;
    }

    // 2. Measure timestamp and compute loop jitter
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!first_read_) {
        double elapsed_us = (now.tv_sec - last_ts_.tv_sec) * 1e6 + (now.tv_nsec - last_ts_.tv_nsec) * 1e-3;
        double jitter_us = std::abs(elapsed_us - 1000.0);
        current_jitter_us_ = jitter_us;
        if (jitter_us > max_jitter_us_) max_jitter_us_ = jitter_us;
        if (jitter_us < min_jitter_us_) min_jitter_us_ = jitter_us;
        total_jitter_us_ += jitter_us;
        jitter_samples_++;
    }
    last_ts_ = now;

    // 3. Read Hardware Registers
    uint32_t raw_left = uio_dev_->read32(REG_LEFT_ENCODER_OFFSET);
    uint32_t raw_right = uio_dev_->read32(REG_RIGHT_ENCODER_OFFSET);

    // 4. Compute delta with robust 32-bit wrap-around handling
    if (first_read_) {
        prev_raw_left_ = raw_left;
        prev_raw_right_ = raw_right;
        first_read_ = false;
    }

    delta_left_ = static_cast<int32_t>(raw_left - prev_raw_left_);
    delta_right_ = static_cast<int32_t>(raw_right - prev_raw_right_);
    prev_raw_left_ = raw_left;
    prev_raw_right_ = raw_right;

    left_pos_ += delta_left_;
    right_pos_ += delta_right_;
    left_vel_ = static_cast<double>(delta_left_) * 1000.0;
    right_vel_ = static_cast<double>(delta_right_) * 1000.0;

    // 5. Acknowledge IRQ to clear FPGA interrupt line
    uio_dev_->write32(REG_INT_ACK_OFFSET, 1);

    // 6. Unmask UIO interrupt for subsequent events
    uint32_t unmask = 1;
    ::write(uio_dev_->get_fd(), &unmask, sizeof(unmask));

    return hardware_interface::return_type::OK;
}

hardware_interface::return_type UioRobotHardware::write(
    const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/) {
    if (!uio_dev_) return hardware_interface::return_type::ERROR;

    // Write PWM commands (16-bit signed)
    int16_t left_pwm = static_cast<int16_t>(left_cmd_);
    int16_t right_pwm = static_cast<int16_t>(right_cmd_);
    if (estop_active_) {
        left_pwm = 0;
        right_pwm = 0;
    }
    uio_dev_->write32(REG_LEFT_PWM_OFFSET, static_cast<uint32_t>(left_pwm));
    uio_dev_->write32(REG_RIGHT_PWM_OFFSET, static_cast<uint32_t>(right_pwm));
    return hardware_interface::return_type::OK;
}

void UioRobotHardware::trigger_estop(bool enable) {
    estop_active_ = enable;
    if (!uio_dev_) return;
    uint32_t ctrl = uio_dev_->read32(REG_CONTROL_OFFSET);
    if (enable) {
        ctrl |= (1 << 1); // bit 1: ESTOP
    } else {
        ctrl &= ~(1 << 1);
    }
    uio_dev_->write32(REG_CONTROL_OFFSET, ctrl);
}

void UioRobotHardware::reset_encoders() {
    if (!uio_dev_) return;
    uint32_t ctrl = uio_dev_->read32(REG_CONTROL_OFFSET);
    uio_dev_->write32(REG_CONTROL_OFFSET, ctrl | (1 << 2)); // bit 2: RESET
    left_pos_ = 0.0;
    left_vel_ = 0.0;
    right_pos_ = 0.0;
    right_vel_ = 0.0;
    delta_left_ = 0;
    delta_right_ = 0;
    first_read_ = true;
}

uint32_t UioRobotHardware::get_status_reg() const {
    return uio_dev_ ? uio_dev_->read32(REG_STATUS_OFFSET) : 0;
}

uint32_t UioRobotHardware::get_cycle_count() const {
    return uio_dev_ ? uio_dev_->read32(REG_CYCLE_CNT_OFFSET) : 0;
}

} // namespace fbb_robot
