#include "button.h"

#include "../utils/output.h"

Button::Button(const std::string name, const gpio_num_t number)
    : Module(button, name), number(number)
{
    gpio_reset_pin(number);
    gpio_set_direction(number, GPIO_MODE_INPUT);
    this->properties["level"] = new IntegerVariable(gpio_get_level(this->number));
    this->properties["change"] = new IntegerVariable();
}

void Button::step()
{
    const int new_level = gpio_get_level(this->number);
    this->properties.at("change")->integer_value = new_level - this->properties.at("level")->integer_value;
    this->properties.at("level")->integer_value = new_level;
    Module::step();
}

void Button::call(const std::string method_name, const std::vector<const Expression *> arguments)
{
    if (method_name == "get")
    {
        Module::expect(arguments, 0);
        echo(all, text, "%s %d", this->name.c_str(), gpio_get_level(this->number));
    }
    else if (method_name == "pullup")
    {
        Module::expect(arguments, 0);
        gpio_set_pull_mode(this->number, GPIO_PULLUP_ONLY);
    }
    else if (method_name == "pulldown")
    {
        Module::expect(arguments, 0);
        gpio_set_pull_mode(this->number, GPIO_PULLDOWN_ONLY);
    }
    else
    {
        Module::call(method_name, arguments);
    }
}

std::string Button::get_output() const
{
    static char buffer[256];
    std::sprintf(buffer, "%d", gpio_get_level(this->number));
    return buffer;
}
