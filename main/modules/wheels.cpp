#include "wheels.h"

const std::map<std::string, Variable_ptr> Wheels::get_defaults() {
    return {
        {"width", std::make_shared<NumberVariable>(1.0)},
        {"linear_speed", std::make_shared<NumberVariable>()},
        {"angular_speed", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
    };
}

Wheels::Wheels(const std::string name, const std::map<std::string, Variable_ptr> &defaults)
    : Module(name) {
    this->properties = defaults;
}

void Wheels::update_speeds(double left_speed, double right_speed) {
    this->properties.at("linear_speed")->number_value = (left_speed + right_speed) / 2;
    this->properties.at("angular_speed")->number_value = (right_speed - left_speed) / this->properties.at("width")->number_value;
}

void Wheels::step() {
    this->update_odometry();

    if (this->properties.at("enabled")->boolean_value != this->last_applied_enabled) {
        if (this->properties.at("enabled")->boolean_value) {
            this->enable();
        } else {
            this->disable();
        }
    }

    Module::step();
}

void Wheels::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "speed") {
        Module::expect(arguments, 2, numbery, numbery);
        if (!this->properties.at("enabled")->boolean_value) {
            return;
        }
        const double linear = arguments[0]->evaluate_number();
        const double angular = arguments[1]->evaluate_number();
        const double width = this->properties.at("width")->number_value;
        this->do_wheel_speeds(linear - angular * width / 2.0, linear + angular * width / 2.0);
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

void Wheels::enable() {
    this->last_applied_enabled = true;
    this->properties.at("enabled")->boolean_value = true;
    this->do_enable();
}

void Wheels::disable() {
    this->do_disable();
    this->last_applied_enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}
