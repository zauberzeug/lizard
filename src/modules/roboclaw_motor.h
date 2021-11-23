#pragma once

#include <string>

#include "module.h"
#include "roboclaw.h"

class RoboClawMotor : public Module
{
private:
    RoboClaw *roboclaw;
    unsigned int motor_number;

public:
    RoboClawMotor(std::string name, RoboClaw *roboclaw, unsigned int motor_number);
    void step();
    void call(std::string method_name, std::vector<Expression *> arguments);
};