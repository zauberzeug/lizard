#pragma once

#include "driver/gpio.h"
#include "module.h"

class Led : public Module
{
private:
    const gpio_num_t number;

public:
    Led(const std::string name, const gpio_num_t number);
    void call(const std::string method_name, const std::vector<const Expression *> arguments);
};
