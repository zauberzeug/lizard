#include "input.h"
#include "../utils/echo.h"

Input::Input(const std::string name, const gpio_num_t number)
    : Module(input, name), number(number) {
    gpio_reset_pin(number);
    gpio_set_direction(number, GPIO_MODE_INPUT);
    this->properties["level"] = new IntegerVariable(gpio_get_level(this->number));
    this->properties["change"] = new IntegerVariable();
}

void Input::step() {
    const int new_level = gpio_get_level(this->number);
    this->properties.at("change")->integer_value = new_level - this->properties.at("level")->integer_value;
    this->properties.at("level")->integer_value = new_level;
    Module::step();
}

void Input::call(const std::string method_name, const std::vector<const Expression *> arguments) {
    if (method_name == "get") {
        Module::expect(arguments, 0);
        echo(up, text, "%s %d", this->name.c_str(), gpio_get_level(this->number));
    } else if (method_name == "pullup") {
        Module::expect(arguments, 0);
        gpio_set_pull_mode(this->number, GPIO_PULLUP_ONLY);
    } else if (method_name == "pulldown") {
        Module::expect(arguments, 0);
        gpio_set_pull_mode(this->number, GPIO_PULLDOWN_ONLY);
    } else if (method_name == "pulloff") {
        Module::expect(arguments, 0);
        gpio_set_pull_mode(this->number, GPIO_FLOATING);
    } else {
        Module::call(method_name, arguments);
    }
}

std::string Input::get_output() const {
    static char buffer[256];
    std::sprintf(buffer, "%d", gpio_get_level(this->number));
    return buffer;
}
