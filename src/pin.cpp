#include "pin.h"

Pin::Pin(gpio_num_t number)
{
    this->number = number;
    gpio_reset_pin(number);
    gpio_set_direction(number, GPIO_MODE_OUTPUT); // TODO: INPUT or OUTPUT?
}
