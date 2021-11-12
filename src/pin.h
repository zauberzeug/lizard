#pragma once

#include "driver/gpio.h"

#include "module.h"

class Pin : public Module
{
private:
    gpio_num_t number;

public:
    Pin(gpio_num_t number);
};
