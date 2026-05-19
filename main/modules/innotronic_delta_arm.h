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
    // Hotfix: first reference after power-on yields a wrong calibration (motor left ends up
    // off-position, twitching). Run "both" as a warmup (lifts both arms; result is ignored),
    // then re-reference left and right individually — only those singles update calibrated_*.
    // Flag is true throughout the whole both → left → right chain so each phase knows to advance.
    bool in_both_sequence = false;
    bool brake_sent_left = false;
    bool brake_sent_right = false;
    int last_ref_left = 0;
    int last_ref_right = 0;
    unsigned long cal_started_at = 0;
    std::string pending_ref_side;
    unsigned long last_backoff_at = 0;
    bool backoff_last_was_left = false;
    double backoff_start_left_deg = 0.0;
    double backoff_start_right_deg = 0.0;

    double target_left_deg = 0.0;
    double target_right_deg = 0.0;
    bool is_settling = false;
    unsigned long stable_since = 0;
    bool left_endstop_prev = false;
    bool right_endstop_prev = false;
    unsigned long stall_since = 0;
    bool was_stalling = false;
    double stall_start_l_deg = 0.0;
    double stall_start_r_deg = 0.0;

    bool is_enabled() const;
    bool is_calibrated() const;
    bool is_motors_swapped() const;
    // Channel/select helpers route logical left/right to physical m1/m2 depending on motors_swapped.
    int channel_for_left() const;
    int channel_for_right() const;
    uint8_t select_for_left() const;
    uint8_t select_for_right() const;
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
