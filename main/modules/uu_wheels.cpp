#include "uu_wheels.h"
#include "../utils/timing.h"
#include <memory>

REGISTER_MODULE_DEFAULTS(UUWheels)

const std::map<std::string, Variable_ptr> UUWheels::get_defaults() {
    return {
        {"width", std::make_shared<NumberVariable>(1.0)},
        {"linear_speed", std::make_shared<NumberVariable>()},
        {"angular_speed", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
    };
}

UUWheels::UUWheels(const std::string name, const UUMotor_ptr left_motor, const UUMotor_ptr right_motor)
    : Module(uu_wheels, name), left_motor(left_motor), right_motor(right_motor) {
    this->properties = UUWheels::get_defaults();
}

void UUWheels::step() {
    const double left_speed = this->left_motor->get_property("speed")->number_value;
    const double right_speed = this->right_motor->get_property("speed")->number_value;

    this->properties.at("linear_speed")->number_value = (left_speed + right_speed) / 2;
    this->properties.at("angular_speed")->number_value = (right_speed - left_speed) / this->properties.at("width")->number_value;

    Module::step();
}

void UUWheels::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "speed") {
        Module::expect(arguments, 2, numbery, numbery);
        if (this->properties.at("enabled")->boolean_value) {
            double linear = arguments[0]->evaluate_number();
            double angular = arguments[1]->evaluate_number();
            double width = this->properties.at("width")->number_value;
            this->left_motor->set_speed(linear - angular * width / 2.0);
            this->right_motor->set_speed(linear + angular * width / 2.0);
        }
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->left_motor->off();
        this->right_motor->off();
    } else if (method_name == "reset_estop") {
        Module::expect(arguments, 0);
        this->left_motor->reset_estop();
        this->right_motor->reset_estop();
    } else {
        Module::call(method_name, arguments);
    }
}
