#include "led.h"

Led::Led(std::string name, gpio_num_t number) : Module(led, name)
{
    this->number = number;
    gpio_reset_pin(number);
    gpio_set_direction(number, GPIO_MODE_OUTPUT);
}

void Led::call(std::string method_name, std::vector<Argument *> arguments)
{
    if (method_name == "on")
    {
        if (arguments.size() != 0)
        {
            printf("error: expecting no arguments for method \"%s.%s\"\n", this->name.c_str(), method_name.c_str());
            return;
        }
        gpio_set_level(this->number, 1);
    }
    else if (method_name == "off")
    {
        if (arguments.size() != 0)
        {
            printf("error: expecting no arguments for method \"%s.%s\"\n", this->name.c_str(), method_name.c_str());
            return;
        }
        gpio_set_level(this->number, 0);
    }
    else
    {
        Module::call(method_name, arguments);
    }
}

void Led::set(std::string property_name, double value)
{
    if (property_name == "level")
    {
        gpio_set_level(this->number, value);
    }
    else
    {
        Module::set(property_name, value);
    }
}
