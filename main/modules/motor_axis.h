#pragma once

#include "input.h"
#include "motor.h"

class MotorAxis : public Module {
private:
    const Motor_ptr motor;
    const Input_ptr input1;
    const Input_ptr input2;

    bool can_move(const float speed) const;

public:
    MotorAxis(const std::string name, const Motor_ptr motor, const Input_ptr input1, const Input_ptr input2);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
};
