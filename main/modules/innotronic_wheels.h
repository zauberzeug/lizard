#pragma once

#include "innotronic_motor.h"
#include "module.h"

class InnotronicWheels : public Module {
private:
    const InnotronicMotor_ptr left_motor;
    const InnotronicMotor_ptr right_motor;

    bool initialized = false;
    unsigned long int last_micros = 0;
    double last_left_position = 0;
    double last_right_position = 0;
    bool enabled = true;

public:
    InnotronicWheels(const std::string name, const InnotronicMotor_ptr left_motor, const InnotronicMotor_ptr right_motor);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
    void enable();
    void disable();
};
