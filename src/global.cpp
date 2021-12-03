#include "global.h"

#include <stdexcept>

std::map<const std::string, Module *> Global::modules;
std::map<const std::string, Routine *> Global::routines;
std::list<Rule *> Global::rules;
std::map<const std::string, Variable *> Global::variables{
    {"Core", new IdentifierVariable("Core")},
    {"Input", new IdentifierVariable("Input")},
    {"Output", new IdentifierVariable("Output")},
    {"Can", new IdentifierVariable("Can")},
    {"Serial", new IdentifierVariable("Serial")},
    {"RmdMotor", new IdentifierVariable("RmdMotor")},
    {"RoboClaw", new IdentifierVariable("RoboClaw")},
    {"RoboClawMotor", new IdentifierVariable("RoboClawMotor")},
    {"Proxy", new IdentifierVariable("Proxy")},
};

Module *Global::get_module(const std::string module_name)
{
    if (!modules.count(module_name))
    {
        throw std::runtime_error("unknown module \"" + module_name + "\"");
    }
    return modules[module_name];
}

Routine *Global::get_routine(const std::string routine_name)
{
    if (!routines.count(routine_name))
    {
        throw std::runtime_error("unknown routine \"" + routine_name + "\"");
    }
    return routines[routine_name];
}

Variable *Global::get_variable(const std::string variable_name)
{
    if (!variables.count(variable_name))
    {
        throw std::runtime_error("unknown variable \"" + variable_name + "\"");
    }
    return variables[variable_name];
}

void Global::add_module(const std::string module_name, Module *const module)
{
    if (modules.count(module_name))
    {
        throw std::runtime_error("module \"" + module_name + "\" already exists");
    }
    if (variables.count(module_name))
    {
        throw std::runtime_error("variable \"" + module_name + "\" already exists");
    }
    modules[module_name] = module;
    variables[module_name] = new IdentifierVariable(module_name);
}

void Global::add_routine(const std::string routine_name, Routine *const routine)
{
    if (routines.count(routine_name))
    {
        throw std::runtime_error("routine \"" + routine_name + "\" already exists");
    }
    routines[routine_name] = routine;
}

void Global::add_variable(const std::string variable_name, Variable *const variable)
{
    if (variables.count(variable_name))
    {
        throw std::runtime_error("variable \"" + variable_name + "\" already exists");
    }
    variables[variable_name] = variable;
}

void Global::add_rule(Rule *const rule)
{
    rules.push_back(rule);
}

bool Global::has_module(const std::string module_name)
{
    return modules.count(module_name);
}

bool Global::has_routine(const std::string routine_name)
{
    return routines.count(routine_name);
}

bool Global::has_variable(const std::string variable_name)
{
    return variables.count(variable_name);
}
