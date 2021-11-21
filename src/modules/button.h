#pragma once

#include <string>
#include <vector>
#include "driver/gpio.h"

#include "../compilation/expression.h"
#include "module.h"

class Button : public Module
{
private:
    gpio_num_t number;

public:
    Button(std::string name, gpio_num_t number);
    void step();
    void call(std::string method_name, std::vector<Expression *> arguments);
    std::string get_output();
};
