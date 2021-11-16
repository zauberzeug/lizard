#pragma once

#include <map>
#include <string>
#include <vector>
#include "../compilation/argument.h"

enum ModuleType
{
    led,
    button,
    serial,
    roboclaw,
};

class Module
{
public:
    ModuleType type;
    std::string name;
    bool output = false;

    Module(ModuleType type, std::string name);
    static Module *create(std::string type,
                          std::string name,
                          std::vector<Argument *> arguments,
                          std::map<std::string, Module *> existing_modules);
    virtual void step();
    virtual void call(std::string method, std::vector<Argument *> arguments);
    virtual double get(std::string property_name);
    virtual std::string get_output();
};