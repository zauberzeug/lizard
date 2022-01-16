#pragma once

#include "can.h"
#include "module.h"

class RmdMotor : public Module {
private:
    const uint32_t can_id;
    Can *const can;
    uint8_t last_msg_id = 0;
    unsigned long int last_msg_millis = 0;

    const RmdMotor *map_leader = nullptr;
    double map_scale = 1;
    double map_offset = 0;
    unsigned long int last_step_time = 0;

    void send_and_wait(const uint32_t id,
                       const uint8_t d0, const uint8_t d1, const uint8_t d2, const uint8_t d3,
                       const uint8_t d4, const uint8_t d5, const uint8_t d6, const uint8_t d7,
                       const unsigned long int timeout_ms = 1);

public:
    RmdMotor(const std::string name, Can *const can, const uint8_t motor_id);
    void step();
    void call(const std::string method_name, const std::vector<Expression_ptr> arguments);
    void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data);
};