/**
 * @file amr_kinematics.cpp
 * @brief Implementation of Differential Drive Kinematics and Odometry Engine.
 */

#include "amr_kinematics.hpp"

namespace fbb_amr {

    AmrKinematics::AmrKinematics(double wheel_radius, double wheel_base, double encoder_cpr)
        : wheel_radius_(wheel_radius), wheel_base_(wheel_base), encoder_cpr_(encoder_cpr) {
        reset_odometry();
    }

    double AmrKinematics::normalize_angle(double angle) {
        while (angle > M_PI)  angle -= 2.0 * M_PI;
        while (angle < -M_PI) angle += 2.0 * M_PI;
        return angle;
    }

    void AmrKinematics::reset_odometry(double x, double y, double theta) {
        pose_.x = x;
        pose_.y = y;
        pose_.theta = normalize_angle(theta);
        current_twist_.linear = 0.0;
        current_twist_.angular = 0.0;
        total_distance_ = 0.0;
    }

    WheelSpeeds AmrKinematics::twist_to_wheel_speeds(double linear_v, double angular_w) const {
        WheelSpeeds ws;
        // Differential drive inverse kinematics: v_L = v - (L/2)*w, v_R = v + (L/2)*w
        ws.left_mps  = linear_v - (wheel_base_ * 0.5) * angular_w;
        ws.right_mps = linear_v + (wheel_base_ * 0.5) * angular_w;

        // Angular velocities (rad/s) = v / r
        ws.left_radps  = ws.left_mps / wheel_radius_;
        ws.right_radps = ws.right_mps / wheel_radius_;

        // Ticks per second = (rad/s / 2pi) * CPR
        double ticks_per_rad = encoder_cpr_ / (2.0 * M_PI);
        ws.left_ticks_s  = ws.left_radps * ticks_per_rad;
        ws.right_ticks_s = ws.right_radps * ticks_per_rad;

        return ws;
    }

    Twist2D AmrKinematics::wheel_speeds_to_twist(double left_mps, double right_mps) const {
        Twist2D tw;
        // Forward kinematics: v = (v_R + v_L)/2, w = (v_R - v_L)/L
        tw.linear  = (right_mps + left_mps) * 0.5;
        tw.angular = (right_mps - left_mps) / wheel_base_;
        return tw;
    }

    void AmrKinematics::update_odometry(int32_t delta_ticks_left, int32_t delta_ticks_right, double dt_seconds) {
        if (dt_seconds <= 0.0) return;

        double meters_per_tick = (2.0 * M_PI * wheel_radius_) / encoder_cpr_;
        double dist_left  = static_cast<double>(delta_ticks_left) * meters_per_tick;
        double dist_right = static_cast<double>(delta_ticks_right) * meters_per_tick;

        double delta_s = (dist_right + dist_left) * 0.5;
        double delta_theta = (dist_right - dist_left) / wheel_base_;

        // Runge-Kutta 2nd order integration:
        // Position update using heading at mid-step
        double mid_theta = pose_.theta + (delta_theta * 0.5);
        pose_.x += delta_s * std::cos(mid_theta);
        pose_.y += delta_s * std::sin(mid_theta);
        pose_.theta = normalize_angle(pose_.theta + delta_theta);

        total_distance_ += std::abs(delta_s);

        // Update current linear and angular speed
        current_twist_.linear  = delta_s / dt_seconds;
        current_twist_.angular = delta_theta / dt_seconds;
    }

} // namespace fbb_amr
