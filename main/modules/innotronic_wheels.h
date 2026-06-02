#pragma once

#include "innotronic_drive_motor.h"
#include "module.h"

class InnotronicWheels : public Module {
private:
    const InnotronicDriveMotor_ptr left_motor;
    const InnotronicDriveMotor_ptr right_motor;
    bool last_applied_enabled = true;

    bool position_initialized = false;
    unsigned long int last_micros = 0;
    double last_left_position = 0.0;
    double last_right_position = 0.0;

    bool is_enabled() const;
    bool is_drivable() const;

public:
    InnotronicWheels(const std::string name, const InnotronicDriveMotor_ptr left_motor, const InnotronicDriveMotor_ptr right_motor);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
    void enable();
    void disable();
};
