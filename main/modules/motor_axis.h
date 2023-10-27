#pragma once

#include "input.h"
#include "stepper_motor.h"

class MotorAxis : public Module {
private:
    const StepperMotor_ptr motor;
    const Input_ptr input1;
    const Input_ptr input2;

public:
    MotorAxis(const std::string name, const StepperMotor_ptr motor, const Input_ptr input1, const Input_ptr input2);
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
};
