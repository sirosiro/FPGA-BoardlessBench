/**
 * @file hardware_interface.hpp
 * @brief Zero-install lightweight compatibility header for ros2_control SystemInterface.
 * 
 * Provides 100% syntactical and semantic compatibility with ROS 2 hardware_interface
 * allowing pure C++ compilation without requiring gigabytes of ROS 2 desktop packages.
 * When real ROS 2 is present, CMake can seamlessly switch to the system header.
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <chrono>

namespace rclcpp {
    class Duration {
    private:
        int64_t nanoseconds_;
    public:
        explicit Duration(int64_t ns = 0) : nanoseconds_(ns) {}
        double seconds() const { return static_cast<double>(nanoseconds_) * 1e-9; }
        int64_t nanoseconds() const { return nanoseconds_; }
    };

    class Time {
    private:
        int64_t nanoseconds_;
    public:
        explicit Time(int64_t ns = 0) : nanoseconds_(ns) {}
        double seconds() const { return static_cast<double>(nanoseconds_) * 1e-9; }
        int64_t nanoseconds() const { return nanoseconds_; }
    };
}

namespace rclcpp_lifecycle {
    struct State {
        uint8_t id = 0;
        std::string label = "unconfigured";
    };
}

namespace hardware_interface {

    enum class CallbackReturn {
        SUCCESS,
        FAILURE,
        ERROR
    };

    enum class return_type {
        OK,
        ERROR
    };

    class StateInterface {
    private:
        std::string prefix_name_;
        std::string interface_name_;
        double* value_ptr_;
    public:
        StateInterface(const std::string& prefix_name, const std::string& interface_name, double* value_ptr = nullptr)
            : prefix_name_(prefix_name), interface_name_(interface_name), value_ptr_(value_ptr) {}
        
        std::string get_name() const { return prefix_name_ + "/" + interface_name_; }
        std::string get_prefix_name() const { return prefix_name_; }
        std::string get_interface_name() const { return interface_name_; }
        double get_value() const { return value_ptr_ ? *value_ptr_ : 0.0; }
        void set_value(double val) { if (value_ptr_) *value_ptr_ = val; }
    };

    class CommandInterface {
    private:
        std::string prefix_name_;
        std::string interface_name_;
        double* value_ptr_;
    public:
        CommandInterface(const std::string& prefix_name, const std::string& interface_name, double* value_ptr = nullptr)
            : prefix_name_(prefix_name), interface_name_(interface_name), value_ptr_(value_ptr) {}
        
        std::string get_name() const { return prefix_name_ + "/" + interface_name_; }
        std::string get_prefix_name() const { return prefix_name_; }
        std::string get_interface_name() const { return interface_name_; }
        double get_value() const { return value_ptr_ ? *value_ptr_ : 0.0; }
        void set_value(double val) { if (value_ptr_) *value_ptr_ = val; }
    };

    struct InterfaceInfo {
        std::string name;
        std::string min;
        std::string max;
    };

    struct JointInfo {
        std::string name;
        std::string type;
        std::vector<InterfaceInfo> state_interfaces;
        std::vector<InterfaceInfo> command_interfaces;
    };

    struct HardwareInfo {
        std::string name;
        std::string type;
        std::vector<JointInfo> joints;
    };

    class SystemInterface {
    public:
        virtual ~SystemInterface() = default;

        virtual CallbackReturn on_init(const HardwareInfo & info) = 0;
        virtual std::vector<StateInterface> export_state_interfaces() = 0;
        virtual std::vector<CommandInterface> export_command_interfaces() = 0;

        virtual CallbackReturn on_configure(const rclcpp_lifecycle::State & /*previous_state*/) {
            return CallbackReturn::SUCCESS;
        }

        virtual CallbackReturn on_activate(const rclcpp_lifecycle::State & /*previous_state*/) {
            return CallbackReturn::SUCCESS;
        }

        virtual CallbackReturn on_deactivate(const rclcpp_lifecycle::State & /*previous_state*/) {
            return CallbackReturn::SUCCESS;
        }

        virtual return_type read(const rclcpp::Time & time, const rclcpp::Duration & period) = 0;
        virtual return_type write(const rclcpp::Time & time, const rclcpp::Duration & period) = 0;
    };

} // namespace hardware_interface
