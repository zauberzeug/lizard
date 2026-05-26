#pragma once

#include "innotronic_drive_motor.h"
#include "module.h"

class InnotronicWheels : public Module {
private:
    const InnotronicDriveMotor_ptr left_motor;
    const InnotronicDriveMotor_ptr right_motor;
    bool last_applied_enabled = true;

    // Per-side state for position-derived speed. Updated only when the
    // underlying motor position actually changes, so the value is robust
    // against the mismatch between the wheel module step rate and the
    // motor's DriveStatus rate.
    struct SideState {
        bool initialized = false;
        double last_position = 0.0;
        unsigned long int last_update_micros = 0;
        double last_calc_speed = 0.0;
    };
    SideState left_state;
    SideState right_state;

    bool is_enabled() const;
    void update_calc_side(SideState &state, double current_position,
                          unsigned long int now_micros,
                          unsigned long int timeout_micros);

public:
    InnotronicWheels(const std::string name, const InnotronicDriveMotor_ptr left_motor, const InnotronicDriveMotor_ptr right_motor);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
    void enable();
    void disable();
};
