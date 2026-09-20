/**
 * @file uio_robot_hardware.hpp
 * @brief ros2_control SystemInterface hardware plugin using standard Linux UIO (/dev/uio0).
 * 
 * Demonstrates 100% hardware transparency: this exact C++ implementation can run on
 * real hardware (Kria KR260 / Zynq-7000) or F-BB simulation without any code changes.
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>
#include <cmath>

#include "hardware_interface/hardware_interface.hpp"

// Standard UIO Register Map
#define UIO_DEV_PATH              "/dev/uio0"
#define MMIO_REGION_SIZE          0x1000

#define REG_STATUS_OFFSET         0x00  // RO: Status (bit0: FAULT/ESTOP, bit1: ENABLED, bit2: IRQ)
#define REG_LEFT_ENCODER_OFFSET   0x04  // RO: Left encoder accumulator
#define REG_RIGHT_ENCODER_OFFSET  0x08  // RO: Right encoder accumulator
#define REG_INT_ACK_OFFSET        0x0C  // WO: IRQ Acknowledge
#define REG_CONTROL_OFFSET        0x10  // RW: Control (bit0: RUN, bit1: ESTOP, bit2: RESET, bit3: TEST)
#define REG_LEFT_PWM_OFFSET       0x14  // RW: Left PWM (-1000 to +1000)
#define REG_RIGHT_PWM_OFFSET      0x18  // RW: Right PWM (-1000 to +1000)
#define REG_CYCLE_CNT_OFFSET      0x1C  // RO: 1kHz cycle tick count

namespace fbb_robot {

/**
 * @brief RAII Memory-Mapped UIO Device Handler (Inherited from S01_cpp_lfsr_sequencer pattern)
 */
class UioDevice {
private:
    int fd_;
    void* base_;
    size_t size_;

public:
    UioDevice(const std::string& path, size_t size) : fd_(-1), base_(MAP_FAILED), size_(size) {
        fd_ = open(path.c_str(), O_RDWR | O_SYNC);
        if (fd_ < 0) {
            throw std::runtime_error("Failed to open UIO device: " + path);
        }
        base_ = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (base_ == MAP_FAILED) {
            close(fd_);
            throw std::runtime_error("Failed to mmap UIO register region: " + path);
        }
    }

    ~UioDevice() {
        if (base_ != MAP_FAILED) munmap(base_, size_);
        if (fd_ >= 0) close(fd_);
    }

    int get_fd() const { return fd_; }

    inline void write32(uint32_t offset, uint32_t value) {
        *((volatile uint32_t*)((uint8_t*)base_ + offset)) = value;
    }

    inline uint32_t read32(uint32_t offset) const {
        return *((volatile uint32_t*)((uint8_t*)base_ + offset));
    }
};

/**
 * @brief ros2_control SystemInterface implementation
 */
class UioRobotHardware : public hardware_interface::SystemInterface {
private:
    std::unique_ptr<UioDevice> uio_dev_;

    // States exported to ros2_control
    double left_pos_ = 0.0;
    double left_vel_ = 0.0;
    double right_pos_ = 0.0;
    double right_vel_ = 0.0;

    // Commands received from ros2_control
    double left_cmd_ = 0.0;
    double right_cmd_ = 0.0;

    // Encoder tracking
    uint32_t prev_raw_left_ = 0;
    uint32_t prev_raw_right_ = 0;
    int32_t delta_left_ = 0;
    int32_t delta_right_ = 0;
    bool first_read_ = true;
    bool estop_active_ = false;

    // Jitter & Performance metrics
    struct timespec last_ts_ = {0, 0};
    double max_jitter_us_ = 0.0;
    double min_jitter_us_ = 999999.0;
    double total_jitter_us_ = 0.0;
    double current_jitter_us_ = 0.0;
    uint32_t jitter_samples_ = 0;

public:
    UioRobotHardware() = default;
    ~UioRobotHardware() override;

    hardware_interface::CallbackReturn on_init(const hardware_interface::HardwareInfo & info) override;
    std::vector<hardware_interface::StateInterface> export_state_interfaces() override;
    std::vector<hardware_interface::CommandInterface> export_command_interfaces() override;

    hardware_interface::CallbackReturn on_activate(const rclcpp_lifecycle::State & previous_state) override;
    hardware_interface::CallbackReturn on_deactivate(const rclcpp_lifecycle::State & previous_state) override;

    hardware_interface::return_type read(const rclcpp::Time & time, const rclcpp::Duration & period) override;
    hardware_interface::return_type write(const rclcpp::Time & time, const rclcpp::Duration & period) override;

    // Diagnostics and Verification Helpers
    UioDevice* get_uio() { return uio_dev_.get(); }
    double get_max_jitter_us() const { return max_jitter_us_; }
    double get_avg_jitter_us() const { return jitter_samples_ > 0 ? (total_jitter_us_ / jitter_samples_) : 0.0; }
    double get_current_jitter_us() const { return current_jitter_us_; }
    void trigger_estop(bool enable);
    bool is_estop_active() const { return estop_active_; }
    void reset_encoders();
    uint32_t get_status_reg() const;
    uint32_t get_cycle_count() const;
    double get_left_pos() const { return left_pos_; }
    double get_right_pos() const { return right_pos_; }
    double get_left_vel() const { return left_vel_; }
    double get_right_vel() const { return right_vel_; }
    double get_left_cmd() const { return left_cmd_; }
    double get_right_cmd() const { return right_cmd_; }
    int32_t get_delta_left() const { return delta_left_; }
    int32_t get_delta_right() const { return delta_right_; }
};

} // namespace fbb_robot
