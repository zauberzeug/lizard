#pragma once

#include "can.h"
#include "module.h"
#include <memory>

class ODriveMotor;
using ODriveMotor_ptr = std::shared_ptr<ODriveMotor>;

class ODriveMotor : public Module, public std::enable_shared_from_this<ODriveMotor> {
private:
    const uint32_t can_id;
    const Can_ptr can;
    bool is_boot_complete = false;
    uint8_t axis_state = -1;
    uint8_t axis_control_mode = -1;
    uint8_t axis_input_mode = -1;

    void set_mode(const uint8_t state, const uint8_t control_mode = 0, const uint8_t input_mode = 0);
    void update_motor_error();

public:
    ODriveMotor(const std::string name, const Can_ptr can, const uint32_t can_id);
    void subscribe_to_can();
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) override;
    void power(const float torque);
    void speed(const float speed);
    void position(const float position);
    void limits(const float speed, const float current);
    void off();
    void reset_motor();
    void clear_errors();
    double get_position();
    void step() override;
};