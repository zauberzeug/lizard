#include "dunker_wheels.h"
#include <memory>

DunkerWheels::DunkerWheels(const std::string name, const DunkerMotor_ptr left_motor, const DunkerMotor_ptr right_motor)
    : Module(dunker_wheels, name), left_motor(left_motor), right_motor(right_motor) {
    this->properties["width"] = std::make_shared<NumberVariable>(1);
    this->properties["linear_speed"] = std::make_shared<NumberVariable>();
    this->properties["angular_speed"] = std::make_shared<NumberVariable>();
}

void DunkerWheels::step() {
    const double left_speed = this->left_motor->get_speed();
    const double right_speed = this->right_motor->get_speed();

    this->properties.at("linear_speed")->number_value = (left_speed + right_speed) / 2;
    this->properties.at("angular_speed")->number_value = (right_speed - left_speed) / this->properties.at("width")->number_value;

    Module::step();
}

void DunkerWheels::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "speed") {
        Module::expect(arguments, 2, numbery, numbery);
        double linear = arguments[0]->evaluate_number();
        double angular = arguments[1]->evaluate_number();
        double width = this->properties.at("width")->number_value;
        this->left_motor->speed(linear - angular * width / 2.0);
        this->right_motor->speed(linear + angular * width / 2.0);
    } else {
        Module::call(method_name, arguments);
    }
}
