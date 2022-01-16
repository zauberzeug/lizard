#include "linear_motor.h"
#include <memory>

LinearMotor::LinearMotor(const std::string name,
                         const gpio_num_t move_in,
                         const gpio_num_t move_out,
                         const gpio_num_t end_in,
                         const gpio_num_t end_out)
    : Module(output, name), move_in(move_in), move_out(move_out), end_in(end_in), end_out(end_out) {
    gpio_reset_pin(move_in);
    gpio_reset_pin(move_out);
    gpio_reset_pin(end_in);
    gpio_reset_pin(end_out);
    gpio_set_direction(move_in, GPIO_MODE_OUTPUT);
    gpio_set_direction(move_out, GPIO_MODE_OUTPUT);
    gpio_set_direction(end_in, GPIO_MODE_INPUT);
    gpio_set_direction(end_out, GPIO_MODE_INPUT);
    this->properties["in"] = std::make_shared<BooleanVariable>(gpio_get_level(this->end_in) == 1);
    this->properties["out"] = std::make_shared<BooleanVariable>(gpio_get_level(this->end_out) == 1);
}

void LinearMotor::step() {
    this->properties.at("in")->boolean_value = gpio_get_level(this->end_in) == 1;
    this->properties.at("out")->boolean_value = gpio_get_level(this->end_out) == 1;
    Module::step();
}

void LinearMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "in") {
        Module::expect(arguments, 0);
        gpio_set_level(this->move_in, 1);
        gpio_set_level(this->move_out, 0);
    } else if (method_name == "out") {
        Module::expect(arguments, 0);
        gpio_set_level(this->move_in, 0);
        gpio_set_level(this->move_out, 1);
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        gpio_set_level(this->move_in, 0);
        gpio_set_level(this->move_out, 0);
    } else {
        Module::call(method_name, arguments);
    }
}
