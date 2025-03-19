#pragma once

#include "module.h"
#include "uu_motor.h"

class UUWheels : public Module {
private:
    const UUMotor_ptr left_motor;
    const UUMotor_ptr right_motor;

    bool initialized = false;
    unsigned long int last_micros;
    double last_left_position;
    double last_right_position;

public:
    UUWheels(const std::string name, const UUMotor_ptr left_motor, const UUMotor_ptr right_motor);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
