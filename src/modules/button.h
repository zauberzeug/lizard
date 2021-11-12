#pragma once

#include <string>

#include "module.h"

class Button : public Module
{
private:
    gpio_num_t number;

public:
    Button(gpio_num_t number);
    void call(std::string method);
    std::string get_output();
};
