#pragma once

#include <map>
#include <list>
#include <string>
#include <vector>
#include "../compilation/argument.h"

enum ModuleType
{
    core,
    led,
    button,
    serial,
    roboclaw,
    roboclaw_motor,
};

class Module
{
private:
    std::list<Module *> shadow_modules;

public:
    ModuleType type;
    std::string name;
    bool output = false;

    Module(ModuleType type, std::string name);
    static Module *create(std::string type, std::string name, std::vector<Argument *> arguments);
    virtual void step();
    virtual void call(std::string method_name, std::vector<Argument *> arguments);
    void call_with_shadows(std::string method_name, std::vector<Argument *> arguments);
    virtual double get(std::string property_name);
    virtual void set(std::string property_name, double value);
    virtual std::string get_output();
};