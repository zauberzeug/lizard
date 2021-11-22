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
    void step();
    void call(std::string method_name, std::vector<Expression *> arguments);
    void handle_can_msg(uint32_t id, int count, uint8_t *data);
};