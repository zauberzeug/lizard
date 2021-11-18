#pragma once

#include <string>
#include <vector>
#include "driver/gpio.h"

#include "../compilation/argument.h"
#include "module.h"

class Led : public Module
{
private:
    gpio_num_t number;

public:
    Led(std::string name, gpio_num_t number);
    void call(std::string method, std::vector<Argument *> arguments);
    void set(std::string property_name, double value);
};
