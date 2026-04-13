#include "innotronic_wheels.h"
#include "../utils/timing.h"
#include <memory>

REGISTER_MODULE_DEFAULTS(InnotronicWheels)

const std::map<std::string, Variable_ptr> InnotronicWheels::get_defaults() {
    return {
        {"width", std::make_shared<NumberVariable>(1.0)},
        {"linear_speed", std::make_shared<NumberVariable>()},
        {"angular_speed", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
    };
}

InnotronicWheels::InnotronicWheels(const std::string name, const InnotronicMotor_ptr left_motor, const InnotronicMotor_ptr right_motor)
    : Module(innotronic_wheels, name), left_motor(left_motor), right_motor(right_motor) {
    this->properties = InnotronicWheels::get_defaults();
}

void InnotronicWheels::step() {
    if (!this->initialized || micros_since(this->last_micros) >= 100000) {
        this->left_motor->request_angle();
        this->right_motor->request_angle();

        double left_position = this->left_motor->get_position();
        double right_position = this->right_motor->get_position();

        if (this->initialized) {
            unsigned long int d_micros = micros_since(this->last_micros);
            double left_speed = (left_position - this->last_left_position) / d_micros * 1000000;
            double right_speed = (right_position - this->last_right_position) / d_micros * 1000000;
            this->properties.at("linear_speed")->number_value = (left_speed + right_speed) / 2;
            this->properties.at("angular_speed")->number_value = (right_speed - left_speed) / this->properties.at("width")->number_value;
        }

        this->last_micros = micros();
        this->last_left_position = left_position;
        this->last_right_position = right_position;
        this->initialized = true;
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

void InnotronicWheels::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "speed") {
        Module::expect(arguments, 2, numbery, numbery);
        if (this->properties.at("enabled")->boolean_value) {
            double linear = arguments[0]->evaluate_number();
            double angular = arguments[1]->evaluate_number();
            double width = this->properties.at("width")->number_value;
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
    this->enabled = true;
    this->properties.at("enabled")->boolean_value = true;
    this->left_motor->enable();
    this->right_motor->enable();
}

void InnotronicWheels::disable() {
    this->left_motor->disable();
    this->right_motor->disable();
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}
