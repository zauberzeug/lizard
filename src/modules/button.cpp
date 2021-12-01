#include "button.h"

#include "../utils/output.h"

Button::Button(std::string name, gpio_num_t number) : Module(button, name)
{
    this->number = number;
    gpio_reset_pin(number);
    gpio_set_direction(number, GPIO_MODE_INPUT);
    this->properties["level"] = new IntegerVariable(gpio_get_level(this->number));
    this->properties["change"] = new IntegerVariable();
}

void Button::step()
{
    int new_level = gpio_get_level(this->number);
    this->properties["change"]->integer_value = new_level - this->properties["level"]->integer_value;
    this->properties["level"]->integer_value = new_level;
    Module::step();
}

void Button::call(std::string method_name, std::vector<Expression *> arguments)
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

std::string Button::get_output()
{
    static char buffer[256];
    std::sprintf(buffer, "%d", gpio_get_level(this->number));
    return buffer;
}
