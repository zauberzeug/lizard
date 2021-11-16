#pragma once

#include <string>
#include <vector>

#include "../compilation/argument.h"
#include "module.h"

class Button : public Module
{
private:
    gpio_num_t number;

public:
    Button(gpio_num_t number);
    void call(std::string method, std::vector<Argument *> arguments);
    double get(std::string property_name);
    std::string get_output();
};
