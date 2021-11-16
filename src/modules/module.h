#pragma once

#include <map>
#include <string>
#include <vector>
#include "../compilation/argument.h"

class Module
{
public:
    std::string name;
    bool output = false;

    static Module *create(std::string module_type,
                          std::string module_name,
                          std::vector<Argument *> arguments,
                          std::map<std::string, Module *> existing_modules);
    virtual void step();
    virtual void call(std::string method, std::vector<Argument *> arguments);
    virtual double get(std::string property_name);
    virtual std::string get_output();
};