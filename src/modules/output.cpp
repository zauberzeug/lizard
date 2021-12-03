#include "output.h"

Output::Output(const std::string name, const gpio_num_t number)
    : Module(output, name), number(number)
{
    gpio_reset_pin(number);
    gpio_set_direction(number, GPIO_MODE_OUTPUT);
}

void Output::call(const std::string method_name, const std::vector<const Expression *> arguments)
{
    if (method_name == "on")
    {
        Module::expect(arguments, 0);
        gpio_set_level(this->number, 1);
    }
    else if (method_name == "off")
    {
        Module::expect(arguments, 0);
        gpio_set_level(this->number, 0);
    }
    else
    {
        Module::call(method_name, arguments);
    }
}
