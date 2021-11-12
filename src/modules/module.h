#pragma once

#include <string>
#include "driver/gpio.h"

class Module
{
public:
    std::string name;
    bool output = false;

    virtual void step();
    virtual void call(std::string method);
    virtual std::string get_output();
};