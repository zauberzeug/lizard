#pragma once

#include "driver/gpio.h"
#include "module.h"

class Led : public Module
{
private:
    gpio_num_t number;

public:
    Led(std::string name, gpio_num_t number);
    void call(std::string method_name, std::vector<Expression *> arguments);
};
