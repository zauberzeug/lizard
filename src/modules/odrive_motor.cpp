#include "odrive_motor.h"
#include <cstring>

ODriveMotor::ODriveMotor(const std::string name, Can *const can, const uint32_t can_id)
    : Module(odrive_motor, name), can_id(can_id),
      can(can) {
    this->properties["position"] = new NumberVariable();
    this->properties["tick_offset"] = new NumberVariable();
    this->properties["m_per_tick"] = new NumberVariable(1.0);
    this->properties["reversed"] = new BooleanVariable();
    this->can->subscribe(this->can_id + 0x009, this);
}

void ODriveMotor::set_mode(const uint8_t state, const uint8_t control_mode, const uint8_t input_mode) {
    if (this->axis_state != state) {
        this->can->send(this->can_id + 0x007, state, 0, 0, 0, 0, 0, 0, 0);
        this->axis_state = state;
    }
    if (this->axis_control_mode != control_mode ||
        this->axis_input_mode != input_mode) {
        this->can->send(this->can_id + 0x00b, control_mode, 0, 0, 0, input_mode, 0, 0, 0);
        this->axis_control_mode = control_mode;
        this->axis_input_mode = input_mode;
    }
}

void ODriveMotor::call(const std::string method_name, const std::vector<const Expression *> arguments) {
    if (method_name == "zero") {
        Module::expect(arguments, 0);
        this->properties.at("tick_offset")->number_value +=
            this->properties.at("position")->number_value / this->properties.at("m_per_tick")->number_value;
    } else if (method_name == "power") {
        Module::expect(arguments, 1, numbery);
        this->power(arguments[0]->evaluate_number());
    } else if (method_name == "speed") {
        Module::expect(arguments, 1, numbery);
        this->speed(arguments[0]->evaluate_number());
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->off();
    } else {
        Module::call(method_name, arguments);
    }
}

void ODriveMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    switch (id - this->can_id) {
    case 0x009: {
        float tick;
        std::memcpy(&tick, data, 4);
        this->properties.at("position")->number_value =
            (tick - this->properties.at("tick_offset")->number_value) *
            (this->properties.at("reversed")->boolean_value ? -1 : 1) *
            this->properties.at("m_per_tick")->number_value;
    }
    }
}

void ODriveMotor::power(const float torque) {
    this->set_mode(8, 1, 1); // AXIS_STATE_CLOSED_LOOP_CONTROL, CONTROL_MODE_TORQUE_CONTROL, INPUT_MODE_PASSTHROUGH
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int sign = this->properties.at("reversed")->boolean_value ? -1 : 1;
    float torque_ = sign * torque;
    std::memcpy(data, &torque_, 4);
    this->can->send(this->can_id + 0x00e, data); // "Set Input Torque"
}

void ODriveMotor::speed(const float speed) {
    this->set_mode(8, 2, 1); // AXIS_STATE_CLOSED_LOOP_CONTROL, CONTROL_MODE_VELOCITY_CONTROL, INPUT_MODE_PASSTHROUGH
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int sign = this->properties.at("reversed")->boolean_value ? -1 : 1;
    float speed_ = sign * speed / this->properties.at("m_per_tick")->number_value;
    std::memcpy(data, &speed_, 4);
    this->can->send(this->can_id + 0x00d, data); // "Set Input Vel"
}

void ODriveMotor::off() {
    this->set_mode(1); // AXIS_STATE_IDLE
}

double ODriveMotor::get_position() {
    return this->properties.at("position")->number_value;
}