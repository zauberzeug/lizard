#include "odrive_wheels.h"
#include "../utils/timing.h"
#include <memory>

const std::map<std::string, Variable_ptr> ODriveWheels::get_defaults() {
    return {
        {"width", std::make_shared<NumberVariable>(1.0)},
        {"linear_speed", std::make_shared<NumberVariable>()},
        {"angular_speed", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
    };
}

ODriveWheels::ODriveWheels(const std::string name, const ODriveMotor_ptr left_motor, const ODriveMotor_ptr right_motor)
    : Module(odrive_wheels, name), left_motor(left_motor), right_motor(right_motor) {
    auto defaults = ODriveWheels::get_defaults();
    this->properties.insert(defaults.begin(), defaults.end());
}

void ODriveWheels::step() {
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

    Module::step();
}

void ODriveWheels::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "power") {
        Module::expect(arguments, 2, numbery, numbery);
        if (this->properties.at("enabled")->boolean_value) {
            this->left_motor->power(arguments[0]->evaluate_number());
            this->right_motor->power(arguments[1]->evaluate_number());
        }
    } else if (method_name == "speed") {
        Module::expect(arguments, 2, numbery, numbery);
        if (this->properties.at("enabled")->boolean_value) {
            double linear = arguments[0]->evaluate_number();
            double angular = arguments[1]->evaluate_number();
            double width = this->properties.at("width")->number_value;
            this->left_motor->speed(linear - angular * width / 2.0);
            this->right_motor->speed(linear + angular * width / 2.0);
        }
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->left_motor->off();
        this->right_motor->off();
    } else {
        Module::call(method_name, arguments);
    }
}
