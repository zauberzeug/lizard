#include "innotronic_wheels.h"
#include "../utils/timing.h"
#include <memory>

REGISTER_MODULE_DEFAULTS(InnotronicWheels)

const std::map<std::string, Variable_ptr> InnotronicWheels::get_defaults() {
    return {
        {"width", std::make_shared<NumberVariable>(1.0)},
        {"linear_speed", std::make_shared<NumberVariable>()},
        {"angular_speed", std::make_shared<NumberVariable>()},
        {"calculated_linear_speed", std::make_shared<NumberVariable>()},
        {"calculated_angular_speed", std::make_shared<NumberVariable>()},
        {"calculated_speed_timeout", std::make_shared<NumberVariable>(0.3)},
        {"traveled_distance", std::make_shared<NumberVariable>()},
        {"heading", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
    };
}

InnotronicWheels::InnotronicWheels(const std::string name, const InnotronicDriveMotor_ptr left_motor, const InnotronicDriveMotor_ptr right_motor)
    : Module(innotronic_wheels, name), left_motor(left_motor), right_motor(right_motor) {
    this->properties = InnotronicWheels::get_defaults();
}

bool InnotronicWheels::is_enabled() const {
    return this->properties.at("enabled")->boolean_value;
}

void InnotronicWheels::step() {
    const double left_speed = this->left_motor->get_speed();
    const double right_speed = this->right_motor->get_speed();
    const double width = this->properties.at("width")->number_value;
    this->properties.at("linear_speed")->number_value = (left_speed + right_speed) / 2;
    this->properties.at("angular_speed")->number_value = (right_speed - left_speed) / width;

    const double left_position = this->left_motor->get_position();
    const double right_position = this->right_motor->get_position();
    this->properties.at("traveled_distance")->number_value = (left_position + right_position) / 2;
    this->properties.at("heading")->number_value = (right_position - left_position) / width;

    const unsigned long int now = micros();
    const double timeout_s = this->properties.at("calculated_speed_timeout")->number_value;
    const unsigned long int timeout_us = timeout_s > 0
        ? static_cast<unsigned long int>(timeout_s * 1e6)
        : 0;
    this->update_calc_side(this->left_state, left_position, now, timeout_us);
    this->update_calc_side(this->right_state, right_position, now, timeout_us);
    this->properties.at("calculated_linear_speed")->number_value =
        (this->left_state.last_calc_speed + this->right_state.last_calc_speed) / 2;
    this->properties.at("calculated_angular_speed")->number_value =
        (this->right_state.last_calc_speed - this->left_state.last_calc_speed) / width;

    bool desired = this->is_enabled();
    if (desired != this->last_applied_enabled) {
        if (desired) {
            this->enable();
        } else {
            this->disable();
        }
    }

    Module::step();
}

void InnotronicWheels::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "speed") {
        Module::expect(arguments, 2, numbery, numbery);
        if (this->is_enabled()) {
            const double linear = arguments[0]->evaluate_number();
            const double angular = arguments[1]->evaluate_number();
            const double width = this->properties.at("width")->number_value;
            this->left_motor->speed(linear - angular * width / 2.0, 0);
            this->right_motor->speed(linear + angular * width / 2.0, 0);
        }
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->left_motor->disable();
        this->right_motor->disable();
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->left_motor->stop();
        this->right_motor->stop();
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

void InnotronicWheels::enable() {
    this->properties.at("enabled")->boolean_value = true;
    this->last_applied_enabled = true;
    this->left_motor->enable();
    this->right_motor->enable();
}

void InnotronicWheels::disable() {
    this->left_motor->disable();
    this->right_motor->disable();
    this->properties.at("enabled")->boolean_value = false;
    this->last_applied_enabled = false;
}

void InnotronicWheels::update_calc_side(SideState &state, const double current_position,
                                        const unsigned long int now_micros,
                                        const unsigned long int timeout_micros) {
    if (!state.initialized) {
        state.initialized = true;
        state.last_position = current_position;
        state.last_update_micros = now_micros;
        state.last_calc_speed = 0.0;
        return;
    }
    if (current_position != state.last_position) {
        const unsigned long int d_micros = now_micros - state.last_update_micros;
        if (d_micros > 0) {
            state.last_calc_speed =
                (current_position - state.last_position) / static_cast<double>(d_micros) * 1e6;
        }
        state.last_position = current_position;
        state.last_update_micros = now_micros;
    } else if (timeout_micros > 0 && (now_micros - state.last_update_micros) > timeout_micros) {
        state.last_calc_speed = 0.0;
    }
}
