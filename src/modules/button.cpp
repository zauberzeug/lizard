#include "button.h"

Button::Button(gpio_num_t number)
{
    this->number = number;
    gpio_reset_pin(number);
    gpio_set_direction(number, GPIO_MODE_INPUT);
}

void Button::call(std::string method)
{
    if (method == "get")
    {
        printf("%d\n", gpio_get_level(this->number));
    }
    else if (method == "pullup")
    {
        gpio_set_pull_mode(this->number, GPIO_PULLUP_ONLY);
    }
    else if (method == "pulldown")
    {
        gpio_set_pull_mode(this->number, GPIO_PULLDOWN_ONLY);
    }
    else
    {
        printf("error: unknown method \"%s\"\n", method.c_str());
    }
}
