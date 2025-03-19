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
            ESP_LOGI("UUWheels", "Speed: %f, %f", linear - angular * width / 2.0, linear + angular * width / 2.0);
        }
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->left_motor->off();
        this->right_motor->off();
    } else {
        Module::call(method_name, arguments);
    }
}
