#pragma once

#include <string>
#include <vector>
#include "../compilation/argument.h"
#include "driver/gpio.h"

class Module
{
public:
    std::string name;
    bool output = false;

    static Module *create(std::string module_type, std::vector<Argument *> arguments);
    virtual void step();
    virtual void call(std::string method, std::vector<Argument *> arguments);
    virtual double get(std::string property_name);
    virtual std::string get_output();
};