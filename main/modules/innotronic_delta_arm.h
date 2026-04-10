#pragma once

#include "innotronic_motor.h"
#include "input.h"
#include "module.h"

class InnotronicDeltaArm : public Module {
private:
    const InnotronicMotor_ptr motor;
    const Input_ptr left_endstop;
    const Input_ptr right_endstop;
    bool enabled = true;

    enum CalibrationState { cal_idle, cal_left, cal_right, cal_both, cal_verify_left, cal_verify_right, cal_verify_both };
    CalibrationState cal_state = cal_idle;
    bool both_left_done = false;
    bool both_right_done = false;

    bool is_motor_active(bool left) const;
    bool can_move(int16_t left_ticks, int16_t right_ticks) const;
    void start_reference(const std::string &side);
    void enable();
    void disable();

public:
    InnotronicDeltaArm(const std::string name, const InnotronicMotor_ptr motor, const Input_ptr left_endstop, const Input_ptr right_endstop);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
