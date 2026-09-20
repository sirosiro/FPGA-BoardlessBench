/**
 * @file amr_kinematics.hpp
 * @brief Differential Drive Kinematics and Odometry Engine for AMR.
 * 
 * Provides forward/inverse kinematics, Runge-Kutta 2nd order odometry integration,
 * wheel speed conversions, and angle normalization.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace fbb_amr {

    struct Pose2D {
        double x = 0.0;     // meters
        double y = 0.0;     // meters
        double theta = 0.0; // radians [-pi, pi]
    };

    struct Twist2D {
        double linear = 0.0;  // m/s
        double angular = 0.0; // rad/s
    };

    struct WheelSpeeds {
        double left_mps = 0.0;        // m/s
        double right_mps = 0.0;       // m/s
        double left_radps = 0.0;      // rad/s
        double right_radps = 0.0;     // rad/s
        double left_ticks_s = 0.0;    // ticks/s
        double right_ticks_s = 0.0;   // ticks/s
    };

    class AmrKinematics {
    public:
        AmrKinematics(double wheel_radius = 0.033, double wheel_base = 0.16, double encoder_cpr = 4096.0);

        // Inverse Kinematics: (v, w) -> Left/Right wheel target speeds
        WheelSpeeds twist_to_wheel_speeds(double linear_v, double angular_w) const;

        // Forward Kinematics: (v_L, v_R) -> (v, w)
        Twist2D wheel_speeds_to_twist(double left_mps, double right_mps) const;

        // Odometry integration from wheel tick deltas
        void update_odometry(int32_t delta_ticks_left, int32_t delta_ticks_right, double dt_seconds);

        void reset_odometry(double x = 0.0, double y = 0.0, double theta = 0.0);

        // Getters
        Pose2D get_pose() const { return pose_; }
        Twist2D get_current_twist() const { return current_twist_; }
        double get_total_distance() const { return total_distance_; }
        double get_wheel_radius() const { return wheel_radius_; }
        double get_wheel_base() const { return wheel_base_; }
        double get_encoder_cpr() const { return encoder_cpr_; }

        // Utility: Normalize angle to [-pi, pi]
        static double normalize_angle(double angle);

    private:
        double wheel_radius_; // r (m)
        double wheel_base_;   // L (m)
        double encoder_cpr_;  // CPR

        Pose2D pose_;
        Twist2D current_twist_;
        double total_distance_ = 0.0;
    };

} // namespace fbb_amr
