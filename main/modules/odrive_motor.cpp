#include "odrive_motor.h"
#include <cstring>
#include <memory>

enum AXIS_ERROR {
    AXIS_ERROR_NONE = 0,
    AXIS_ERROR_INVALID_STATE = 0x01,
    AXIS_ERROR_WATCHDOG_TIMER_EXPIRED = 0x800,
    AXIS_ERROR_MIN_ENDSTOP_PRESSED = 0x1000,
    AXIS_ERROR_MAX_ENDSTOP_PRESSED = 0x2000,
    AXIS_ERROR_ESTOP_REQUESTED = 0x4000,
    AXIS_ERROR_OVER_TEMP = 0x40000,
    AXIS_ERROR_UNKNOWN_POSITION = 0x80000
};
enum MOTOR_ERROR {
    MOTOR_ERROR_NONE = 0,
    MOTOR_ERROR_PHASE_RESISTANCE_OUT_OF_RANGE = 0x01,
    MOTOR_ERROR_PHASE_INDUCTANCE_OUT_OF_RANGE = 0x02,
    MOTOR_ERROR_DRV_FAULT = 0x08,
    MOTOR_ERROR_CONTROL_DEADLINE_MISSED = 0x10,
    MOTOR_ERROR_MODULATION_MAGNITUDE = 0x80,
    MOTOR_ERROR_BRAKE_CURRENT_OUT_OF_RANGE = 0x400,
    MOTOR_ERROR_CURRENT_LIMIT_VIALTION = 0x1000,
    // TODO add more error coddes https://docs.odriverobotics.com/v/0.5.4/fibre_types/com_odriverobotics_ODrive.html?highlight=motor+error#ODrive.Motor.Error

};

ODriveMotor::ODriveMotor(const std::string name, const Can_ptr can, const uint32_t can_id) : Module(odrive_motor, name),
                                                                                             can_id(can_id), can(can) {
    this->properties["position"] = std::make_shared<NumberVariable>();
    this->properties["tick_offset"] = std::make_shared<NumberVariable>();
    this->properties["m_per_tick"] = std::make_shared<NumberVariable>(1.0);
    this->properties["reversed"] = std::make_shared<BooleanVariable>();
    this->properties["motor_error"] = std::make_shared<IntegerVariable>(0);
    this->properties["axis_error"] = std::make_shared<IntegerVariable>();
}

void ODriveMotor::subscribe_to_can() {
    this->can->subscribe(this->can_id + 0x001, std::static_pointer_cast<Module>(this->shared_from_this()));
    this->can->subscribe(this->can_id + 0x003, std::static_pointer_cast<Module>(this->shared_from_this()));
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
    } else if (method_name == "reset_motor") {
        Module::expect(arguments, 0);
        this->reset_motor();
    } else if (method_name == "clear_errors") {
        Module::expect(arguments, 0);
        this->clear_errors();
    } else {
        Module::call(method_name, arguments);
    }
}

void ODriveMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    this->is_boot_complete = true;
    switch (id - this->can_id) {
    case 0x001: {
        int axis_state;
        std::memcpy(&axis_state, data + 4, 2);
        this->axis_state = axis_state;
        int axis_error;
        std::memcpy(&axis_error, data, 4);
        switch (axis_error) {
        case AXIS_ERROR_INVALID_STATE:
            this->properties.at("axis_error")->integer_value = 1;
            break;
        case AXIS_ERROR_WATCHDOG_TIMER_EXPIRED:
            this->properties.at("axis_error")->integer_value = 2;
            break;
        case AXIS_ERROR_MIN_ENDSTOP_PRESSED:
            this->properties.at("axis_error")->integer_value = 3;
            break;
        case AXIS_ERROR_MAX_ENDSTOP_PRESSED:
            this->properties.at("axis_error")->integer_value = 4;
            break;
        case AXIS_ERROR_ESTOP_REQUESTED:
            this->properties.at("axis_error")->integer_value = 5;
            break;
        case AXIS_ERROR_OVER_TEMP:
            this->properties.at("axis_error")->integer_value = 6;
            break;
        case AXIS_ERROR_UNKNOWN_POSITION:
            this->properties.at("axis_error")->integer_value = 7;
            break;
        default:
            this->properties.at("axis_error")->integer_value = axis_error;

            break;
        }
        break;
    case 0x003: {
        int motor_error;
        std::memcpy(&motor_error, data, 4);
        switch (motor_error) {
        case MOTOR_ERROR_NONE:
            this->properties.at("motor_error")->integer_value = 0;
            break;

        default:
            this->properties.at("motor_error")->integer_value = 1;
            break;
        }
    }
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
void ODriveMotor::clear_errors() {
    uint8_t empty_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    this->can->send(this->can_id + 0x018, empty_data, true);
}
void ODriveMotor::update_motor_error() {
    uint8_t empty_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    this->can->send(this->can_id + 0x003, empty_data, true);
}
void ODriveMotor::reset_motor() {
    this->clear_errors();
    this->set_mode(8, 2, 1); // AXIS_STATE_CLOSED_LOOP_CONTROL, CONTROL_MODE_VELOCITY_CONTROL, INPUT_MODE_PASSTHROUGH
    this->update_motor_error();
}
double ODriveMotor::get_position() {
    return this->properties.at("position")->number_value;
}

void ODriveMotor::step() {
    this->update_motor_error();
    Module::step();
}