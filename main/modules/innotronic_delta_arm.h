#pragma once

#include "innotronic_delta_motor.h"
#include "input.h"
#include "module.h"

class InnotronicDeltaArm : public Module {
private:
    const InnotronicDeltaMotor_ptr motor;
    const Input_ptr left_endstop;
    const Input_ptr right_endstop;
    const double deg_per_tick;
    bool last_applied_enabled = true;

    enum CalibrationState { cal_idle, cal_left, cal_right, cal_both, cal_backoff };
    CalibrationState cal_state = cal_idle;
    bool both_left_done = false;
    bool both_right_done = false;
    bool m1_brake_sent = false;
    bool m2_brake_sent = false;
    int last_ref_m1 = 0;
    int last_ref_m2 = 0;
    unsigned long cal_started_at = 0;
    std::string pending_ref_side;
    unsigned long last_backoff_at = 0;
    bool backoff_last_was_left = false;

    double target_left_deg = 0.0;
    double target_right_deg = 0.0;
    bool was_in_tol = false;
    unsigned long stable_since = 0;
    bool left_endstop_prev = false;
    bool right_endstop_prev = false;
    unsigned long stall_since = 0;
    bool was_stalling = false;
    double stall_start_deg_m1 = 0.0;
    double stall_start_deg_m2 = 0.0;

    bool is_enabled() const;
    bool is_calibrated() const;
    bool endstop_active(const Input_ptr &input) const;
    void move_to(double left_deg, double right_deg, uint8_t speed_left, uint8_t speed_right);
    bool can_move(double left_deg, double right_deg) const;
    void start_reference(const std::string &side);
    void enable();
    void disable();

public:
    InnotronicDeltaArm(const std::string name, const InnotronicDeltaMotor_ptr motor, const Input_ptr left_endstop, const Input_ptr right_endstop);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
