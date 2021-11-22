#pragma once

#include <list>
#include <map>
#include <string>
#include <vector>
#include "../compilation/expression.h"
#include "../compilation/variable.h"

enum ModuleType
{
    button,
    can,
    core,
    led,
    serial,
    rmd_motor,
    roboclaw,
    roboclaw_motor,
};

class Module
{
private:
    std::list<Module *> shadow_modules;

protected:
    std::map<std::string, Variable *> properties;

public:
    ModuleType type;
    std::string name;
    bool output = false;

    Module(ModuleType type, std::string name);
    static void expect(std::vector<Expression *> arguments, int num, ...);
    static Module *create(std::string type, std::string name, std::vector<Expression *> arguments);
    virtual void step();
    virtual void call(std::string method_name, std::vector<Expression *> arguments);
    void call_with_shadows(std::string method_name, std::vector<Expression *> arguments);
    virtual std::string get_output();
    Variable *get_property(std::string property_name);
    virtual void handle_can_msg(uint32_t id, int count, uint8_t *data);
};