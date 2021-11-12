#pragma once

#include <string>

#include "module.h"

class Pin : public Module
{
private:
    gpio_num_t number;

public:
    Pin(gpio_num_t number);
    void call(std::string method);
};
