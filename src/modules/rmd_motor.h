#pragma once

#include "module.h"
#include "can.h"

class RmdMotor : public Module
{
private:
    uint32_t can_id = 0x141;
    Can *can;

public:
    RmdMotor(std::string name, Can *can);
    void call(std::string method_name, std::vector<Expression *> arguments);
};