#pragma once

#include "module.h"
#include "roboclaw.h"

class RoboClawMotor : public Module {
private:
    const unsigned int motor_number;
    RoboClaw *const roboclaw;

public:
    RoboClawMotor(const std::string name, RoboClaw *const roboclaw, const unsigned int motor_number);
    void step();
    void call(const std::string method_name, const std::vector<const Expression *> arguments);
};