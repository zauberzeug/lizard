#include "dunker_wheels.h"
#include <memory>

REGISTER_MODULE_DEFAULTS(DunkerWheels)

const std::map<std::string, Variable_ptr> DunkerWheels::get_defaults() {
    return {
        {"width", std::make_shared<NumberVariable>(1.0)},
        {"linear_speed", std::make_shared<NumberVariable>()},
        {"angular_speed", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
    };
}

DunkerWheels::DunkerWheels(const std::string name, const DunkerMotor_ptr left_motor, const DunkerMotor_ptr right_motor)
    : Module(dunker_wheels, name), left_motor(left_motor), right_motor(right_motor) {
    this->properties = DunkerWheels::get_defaults();
}

void DunkerWheels::step() {
    const double left_speed = this->left_motor->get_speed();
    const double right_speed = this->right_motor->get_speed();

    this->properties.at("linear_speed")->number_value = (left_speed + right_speed) / 2;
    this->properties.at("angular_speed")->number_value = (right_speed - left_speed) / this->properties.at("width")->number_value;

    if (this->properties.at("enabled")->boolean_value != this->enabled) {
        if (this->properties.at("enabled")->boolean_value) {
            this->enable();
        } else {
            this->disable();
        }
    }

    Module::step();
}

void DunkerWheels::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "speed") {
        Module::expect(arguments, 2, numbery, numbery);
        if (!this->enabled)
            return;
        double linear = arguments[0]->evaluate_number();
        double angular = arguments[1]->evaluate_number();
        double width = this->properties.at("width")->number_value;
        this->left_motor->speed(linear - angular * width / 2.0);
        this->right_motor->speed(linear + angular * width / 2.0);
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

void DunkerWheels::enable() {
    this->enabled = true;
    this->properties.at("enabled")->boolean_value = true;
    this->left_motor->enable();
    this->right_motor->enable();
}

void DunkerWheels::disable() {
    this->left_motor->disable();
    this->right_motor->disable();
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}
