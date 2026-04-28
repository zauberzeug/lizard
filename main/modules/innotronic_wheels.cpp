#include "innotronic_wheels.h"
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
