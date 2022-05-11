#pragma once

#include "roboclaw_motor.h"

class RoboClawWheels : public Module {
private:
    const RoboClawMotor_ptr left_motor;
    const RoboClawMotor_ptr right_motor;

public:
    RoboClawWheels(const std::string name, const RoboClawMotor_ptr left_motor, const RoboClawMotor_ptr right_motor);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
};
