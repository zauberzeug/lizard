#pragma once

#include "module.h"
#include "roboclaw.h"

class RoboClawMotor : public Module {
private:
    const unsigned int motor_number;
    const RoboClaw_ptr roboclaw;

public:
    RoboClawMotor(const std::string name, const RoboClaw_ptr roboclaw, const unsigned int motor_number);
    void step();
    void call(const std::string method_name, const std::vector<Expression_ptr> arguments);
};