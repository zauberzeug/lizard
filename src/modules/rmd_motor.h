#pragma once

#include "module.h"
#include "can.h"

class RmdMotor : public Module
{
private:
    uint32_t can_id;
    Can *can;
    void send_and_wait(uint32_t id,
                       uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
                       uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7,
                       unsigned long int timeout_ms = 1);

public:
    RmdMotor(std::string name, Can *can, uint8_t motor_id);
    void step();
    void call(std::string method_name, std::vector<Expression *> arguments);
    void handle_can_msg(uint32_t id, int count, uint8_t *data);
};