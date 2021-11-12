#pragma once

#include "driver/gpio.h"

class Module
{
public:
    virtual void call(std::string method) = 0;
};