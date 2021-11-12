#include "led.h"

Led::Led(gpio_num_t number)
{
    this->number = number;
    gpio_reset_pin(number);
    gpio_set_direction(number, GPIO_MODE_OUTPUT);
}

void Led::call(std::string method)
{
    if (method == "on")
    {
        gpio_set_level(this->number, 1);
    }
    else if (method == "off")
    {
        gpio_set_level(this->number, 0);
    }
    else
    {
        printf("error: unknown method \"%s\"\n", method.c_str());
    }
}
