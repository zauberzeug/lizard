#pragma once

#include "driver/gpio.h"

class Module
{
public:
    std::string name;
    virtual void call(std::string method) = 0;
};