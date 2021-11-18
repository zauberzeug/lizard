#pragma once

#include "module.h"

class Core : public Module
{
public:
    bool debug = false;

    Core(std::string name);
    void call(std::string method, std::vector<Argument *> arguments);
    double get(std::string property_name);
    void set(std::string property_name, double value);
};