#pragma once

#include "can.h"
#include "module.h"

class ODriveMotor : public Module {
private:
    const uint32_t can_id;
    Can *const can;
    uint8_t axis_state = -1;
    uint8_t axis_control_mode = -1;
    uint8_t axis_input_mode = -1;

    void set_mode(const uint8_t state, const uint8_t control_mode = 0, const uint8_t input_mode = 0);

public:
    ODriveMotor(const std::string name, Can *const can, const uint32_t can_id);
    void call(const std::string method_name, const std::vector<const Expression *> arguments);
    void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data);
    void power(const float torque);
    void speed(const float speed);
    void off();
    double get_position();
};