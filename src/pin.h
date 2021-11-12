#pragma once

#include "driver/gpio.h"

class Pin
{
private:
    gpio_num_t number;

public:
    Pin(gpio_num_t number);
};
