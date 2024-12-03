#pragma once

#include "roboclaw_motor.h"

class RoboClawWheels : public Module {
private:
    const RoboClawMotor_ptr left_motor;
    const RoboClawMotor_ptr right_motor;

    unsigned long int last_micros;
    int64_t last_left_position;
    int64_t last_right_position;
    bool initialized = false;

public:
    RoboClawWheels(const std::string name, const RoboClawMotor_ptr left_motor, const RoboClawMotor_ptr right_motor);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
