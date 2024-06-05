#include "odrive_motor.h"
#include <cstring>
#include <memory>

ODriveMotor::ODriveMotor(const std::string name, const Can_ptr can, const uint32_t can_id)
    : Module(odrive_motor, name), can_id(can_id), can(can) {
    this->properties["position"] = std::make_shared<NumberVariable>();
    this->properties["speed"] = std::make_shared<NumberVariable>();
    this->properties["tick_offset"] = std::make_shared<NumberVariable>();
    this->properties["m_per_tick"] = std::make_shared<NumberVariable>(1.0);
    this->properties["reversed"] = std::make_shared<BooleanVariable>();
}

void ODriveMotor::subscribe_to_can() {
    this->can->subscribe(this->can_id + 0x009, std::static_pointer_cast<Module>(this->shared_from_this()));
}

void ODriveMotor::set_mode(const uint8_t state, const uint8_t control_mode, const uint8_t input_mode) {
    if (!this->is_boot_complete) {
        return;
    }
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

void ODriveMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "zero") {
        Module::expect(arguments, 0);
        this->properties.at("tick_offset")->number_value +=
            this->properties.at("position")->number_value /
            this->properties.at("m_per_tick")->number_value *
            (this->properties.at("reversed")->boolean_value ? -1 : 1);
    } else if (method_name == "power") {
        Module::expect(arguments, 1, numbery);
        this->power(arguments[0]->evaluate_number());
    } else if (method_name == "speed") {
        Module::expect(arguments, 1, numbery);
        this->speed(arguments[0]->evaluate_number());
    } else if (method_name == "position") {
        Module::expect(arguments, 1, numbery);
        this->position(arguments[0]->evaluate_number());
    } else if (method_name == "limits") {
        Module::expect(arguments, 2, numbery, numbery);
        this->limits(arguments[0]->evaluate_number(), arguments[1]->evaluate_number());
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->off();
    } else {
        Module::call(method_name, arguments);
    }
}

void ODriveMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    this->is_boot_complete = true;
    switch (id - this->can_id) {
    case 0x009: {
        float tick;
        std::memcpy(&tick, data, 4);
        this->properties.at("position")->number_value =
            (tick - this->properties.at("tick_offset")->number_value) *
            (this->properties.at("reversed")->boolean_value ? -1 : 1) *
            this->properties.at("m_per_tick")->number_value;
        float ticks_per_second;
        std::memcpy(&ticks_per_second, data + 4, 4);
        this->properties.at("speed")->number_value =
            ticks_per_second *
            (this->properties.at("reversed")->boolean_value ? -1 : 1) *
            this->properties.at("m_per_tick")->number_value;
    }
    }
}

void ODriveMotor::power(const float torque) {
    this->set_mode(8, 1, 1); // AXIS_STATE_CLOSED_LOOP_CONTROL, CONTROL_MODE_TORQUE_CONTROL, INPUT_MODE_PASSTHROUGH
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int sign = this->properties.at("reversed")->boolean_value ? -1 : 1;
    const float motor_torque = sign * torque;
    std::memcpy(data, &motor_torque, 4);
    this->can->send(this->can_id + 0x00e, data); // "Set Input Torque"
}

void ODriveMotor::speed(const float speed) {
    this->set_mode(8, 2, 1); // AXIS_STATE_CLOSED_LOOP_CONTROL, CONTROL_MODE_VELOCITY_CONTROL, INPUT_MODE_PASSTHROUGH
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const float motor_speed = speed /
                              this->properties.at("m_per_tick")->number_value /
                              (this->properties.at("reversed")->boolean_value ? -1 : 1);
    std::memcpy(data, &motor_speed, 4);
    this->can->send(this->can_id + 0x00d, data); // "Set Input Vel"
}

void ODriveMotor::position(const float position) {
    this->set_mode(8, 3, 1); // AXIS_STATE_CLOSED_LOOP_CONTROL, CONTROL_MODE_POSITION_CONTROL, INPUT_MODE_PASSTHROUGH
    uint8_t pos_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const float motor_position = position /
                                     (this->properties.at("reversed")->boolean_value ? -1 : 1) /
                                     this->properties.at("m_per_tick")->number_value +
                                 this->properties.at("tick_offset")->number_value;
    std::memcpy(pos_data, &motor_position, 4);
    this->can->send(this->can_id + 0x00c, pos_data); // "Set Input Pos"
}

void ODriveMotor::limits(const float speed, const float current) {
    uint8_t limit_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const float motor_speed = speed / this->properties.at("m_per_tick")->number_value;
    std::memcpy(limit_data, &motor_speed, 4);
    std::memcpy(limit_data + 4, &current, 4);
    this->can->send(this->can_id + 0x00f, limit_data); // "Set Limits"
}

void ODriveMotor::off() {
    this->set_mode(1); // AXIS_STATE_IDLE
}

void ODriveMotor::stop() {
    this->speed(0);
}

double ODriveMotor::get_position() {
    return this->properties.at("position")->number_value;
}

void ODriveMotor::position(const double position, const double speed, const double acceleration) {
    this->position(static_cast<float>(position));
}

double ODriveMotor::get_speed() {
    return this->properties.at("speed")->number_value;
}

void ODriveMotor::speed(const double speed, const double acceleration) {
    this->speed(static_cast<float>(speed));
}