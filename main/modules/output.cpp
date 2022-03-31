#include "output.h"

Output::Output(const std::string name, const gpio_num_t number)
    : Module(output, name), number(number) {
    gpio_reset_pin(number);
    gpio_set_direction(number, GPIO_MODE_OUTPUT);
    this->properties["level"] = std::make_shared<IntegerVariable>();
    this->properties["change"] = std::make_shared<IntegerVariable>();
}

void Output::step() {
    gpio_set_level(this->number, this->target_level);
    this->properties.at("change")->integer_value = this->target_level - this->properties.at("level")->integer_value;
    this->properties.at("level")->integer_value = this->target_level;
}

void Output::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "on") {
        Module::expect(arguments, 0);
        this->target_level = 1;
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->target_level = 0;
    } else if (method_name == "level") {
        Module::expect(arguments, 1, boolean);
        this->target_level = arguments[0]->evaluate_boolean();
    } else {
        Module::call(method_name, arguments);
    }
}
