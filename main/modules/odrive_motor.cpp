#include "odrive_motor.h"
#include "../utils/timing.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string_view>

REGISTER_MODULE_DEFAULTS(ODriveMotor)

const std::map<std::string, Variable_ptr> ODriveMotor::get_defaults() {
    return {
        {"position", std::make_shared<NumberVariable>()},
        {"speed", std::make_shared<NumberVariable>()},
        {"tick_offset", std::make_shared<NumberVariable>()},
        {"m_per_tick", std::make_shared<NumberVariable>(1.0)},
        {"reversed", std::make_shared<BooleanVariable>()},
        {"axis_state", std::make_shared<IntegerVariable>()},
        {"axis_error", std::make_shared<IntegerVariable>()},
        {"motor_error_flag", std::make_shared<IntegerVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
    };
}

// Error codes for ODriveMotor
static constexpr ErrorCode ERROR_NOT_CONNECTED = 0x01;
static constexpr ErrorCode ERROR_HARDWARE_FAULT = 0x02;

// Error table (stored in flash memory, pre-sorted by code)
static constexpr std::array<ErrorEntry, 2> kODriveErrors{{
    {ERROR_NOT_CONNECTED, "Not connected"},  // 0x01 -> bit 0
    {ERROR_HARDWARE_FAULT, "Hardware fault"} // 0x02 -> bit 1
}};

// Check if module has any errors
bool ODriveMotor::has_error() const {
    return error_bitmask_ != 0;
}

// Get error string for this module (returns empty if no errors)
std::string ODriveMotor::get_error() const {
    if (!has_error()) {
        return ""; // No errors
    }

    std::string error_messages;
    bool first = true;

    for (const auto &entry : kODriveErrors) {
        uint8_t bit_position = entry.code - 1; // Convert error code to bit position
        if (error_bitmask_ & (1 << bit_position)) {
            if (!first) {
                error_messages += ", ";
            }
            error_messages += entry.msg;
            first = false;
        }
    }

    return this->name + " - " + error_messages;
}

// Set error by setting the corresponding bit in bitmask
void ODriveMotor::set_error(ErrorCode code) {
    uint8_t bit_position = code - 1; // Convert error code to bit position
    error_bitmask_ |= (1 << bit_position);
    GlobalErrorState::set_error_flag(true);
}

ODriveMotor::ODriveMotor(const std::string name, const Can_ptr can, const uint32_t can_id, const uint32_t version)
    : Module(odrive_motor, name), can_id(can_id), can(can), version(version) {
    this->properties = ODriveMotor::get_defaults();
    this->last_can_message_time = millis();

    // Register this module's error system with GlobalErrorState using lambda
    GlobalErrorState::register_module(this->name.c_str(), [this]() {
        return this->get_error();
    });
}

void ODriveMotor::subscribe_to_can() {
    this->can->subscribe(this->can_id + 0x001, std::static_pointer_cast<Module>(this->shared_from_this()));
    this->can->subscribe(this->can_id + 0x009, std::static_pointer_cast<Module>(this->shared_from_this()));
}

void ODriveMotor::set_mode(const uint8_t state, const uint8_t control_mode, const uint8_t input_mode) {
    if (!this->is_boot_complete) {
        return;
    }
    if (this->properties.at("motor_error_flag")->number_value == 1) {
        this->axis_state = -1;
        this->axis_control_mode = -1;
        this->axis_input_mode = -1;
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
        this->reset_motor_error();
    } else if (method_name == "enable") {
        Module::expect(arguments, 0);
        this->enable();
    } else if (method_name == "disable") {
        Module::expect(arguments, 0);
        this->disable();
    } else {
        Module::call(method_name, arguments);
    }
}

void ODriveMotor::check_connection() {
    const uint32_t connection_timeout_ms = 5000; // 5 seconds
    const uint32_t current_time = millis();

    if (millis_since(this->last_can_message_time) > connection_timeout_ms) {
        if (!this->connection_error_reported) {
            this->set_error(ERROR_NOT_CONNECTED);
            this->connection_error_reported = true;
        }
    }
}

void ODriveMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    this->is_boot_complete = true;
    this->last_can_message_time = millis();
    this->connection_error_reported = false; // Reset error flag since we received a message

    switch (id - this->can_id) {
    case 0x001: {
        int axis_error;
        std::memcpy(&axis_error, data, 4);
        this->properties.at("axis_error")->integer_value = axis_error;
        int axis_state;
        std::memcpy(&axis_state, data + 4, 1);
        this->axis_state = axis_state;
        this->properties.at("axis_state")->integer_value = axis_state;
        if (version == 6) {
            int message_byte;
            std::memcpy(&message_byte, data + 5, 1);
            this->properties.at("motor_error_flag")->integer_value = message_byte & 0x01;
        }
        break;
    }
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
    if (!this->enabled) {
        return;
    }
    this->set_mode(8, 1, 1); // AXIS_STATE_CLOSED_LOOP_CONTROL, CONTROL_MODE_TORQUE_CONTROL, INPUT_MODE_PASSTHROUGH
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int sign = this->properties.at("reversed")->boolean_value ? -1 : 1;
    const float motor_torque = sign * torque;
    std::memcpy(data, &motor_torque, 4);
    this->can->send(this->can_id + 0x00e, data); // "Set Input Torque"
}

void ODriveMotor::speed(const float speed) {
    if (!this->enabled) {
        return;
    }
    this->set_mode(8, 2, 1); // AXIS_STATE_CLOSED_LOOP_CONTROL, CONTROL_MODE_VELOCITY_CONTROL, INPUT_MODE_PASSTHROUGH
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const float motor_speed = speed /
                              this->properties.at("m_per_tick")->number_value /
                              (this->properties.at("reversed")->boolean_value ? -1 : 1);
    std::memcpy(data, &motor_speed, 4);
    this->can->send(this->can_id + 0x00d, data); // "Set Input Vel"
}

void ODriveMotor::position(const float position) {
    if (!this->enabled) {
        return;
    }
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

void ODriveMotor::reset_motor_error() {
    uint8_t empty_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    this->can->send(this->can_id + 0x018, empty_data); // "Clear Errors"
}

void ODriveMotor::step() {
    this->check_connection();

    if (!this->connection_error_reported && this->properties.at("motor_error_flag")->integer_value == 1) {
        this->set_error(ERROR_HARDWARE_FAULT);
    }

    if (this->properties.at("enabled")->boolean_value != this->enabled) {
        if (this->properties.at("enabled")->boolean_value) {
            this->enable();
        } else {
            this->disable();
        }
    }

    Module::step();
}

void ODriveMotor::enable() {
    this->enabled = true;
    this->properties.at("enabled")->boolean_value = true;
    this->reset_motor_error();
}

void ODriveMotor::disable() {
    this->stop();
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;
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
