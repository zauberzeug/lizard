#pragma once

#include "innotronic_motor.h"
#include "module.h"

class InnotronicWheels : public Module {
private:
    const InnotronicMotor_ptr left_motor;
    const InnotronicMotor_ptr right_motor;

    bool enabled = true;

public:
    InnotronicWheels(const std::string name, const InnotronicMotor_ptr left_motor, const InnotronicMotor_ptr right_motor);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
    void enable();
    void disable();
};
