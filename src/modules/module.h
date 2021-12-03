#pragma once

#include <list>
#include <map>
#include <string>
#include <vector>
#include "../compilation/expression.h"
#include "../compilation/variable.h"

enum ModuleType
{
    core,
    input,
    output,
    can,
    serial,
    rmd_motor,
    roboclaw,
    roboclaw_motor,
    proxy,
    number_of_module_types,
};

class Module
{
private:
    std::list<Module *> shadow_modules;

protected:
    std::map<std::string, Variable *> properties;
    bool output_on = false;
    bool broadcast = false;

public:
    const ModuleType type;
    const std::string name;

    Module(const ModuleType type, const std::string name);
    static void expect(const std::vector<const Expression *> arguments, const int num, ...);
    static Module *create(const std::string type, const std::string name, const std::vector<const Expression *> arguments);
    virtual void step();
    virtual void call(const std::string method_name, const std::vector<const Expression *> arguments);
    void call_with_shadows(const std::string method_name, const std::vector<const Expression *> arguments);
    virtual std::string get_output() const;
    Variable *get_property(const std::string property_name) const;
    virtual void write_property(const std::string property_name, const Expression *const expression);
    virtual void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data);
};