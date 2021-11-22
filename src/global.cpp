#include "global.h"

std::map<std::string, Module *> Global::modules;
std::map<std::string, Routine *> Global::routines;
std::list<Rule *> Global::rules;
std::map<std::string, Variable *> Global::variables;

Module *Global::get_module(std::string module_name)
{
    if (!modules.count(module_name))
    {
        throw std::runtime_error("unkown module \"" + module_name + "\"");
    }
    return modules[module_name];
}

Routine *Global::get_routine(std::string routine_name)
{
    if (!routines.count(routine_name))
    {
        throw std::runtime_error("unkown routine \"" + routine_name + "\"");
    }
    return routines[routine_name];
}

Variable *Global::get_variable(std::string variable_name)
{
    if (!variables.count(variable_name))
    {
        throw std::runtime_error("unkown variable \"" + variable_name + "\"");
    }
    return variables[variable_name];
}

void Global::add_module(std::string module_name, Module *module)
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

void Global::add_routine(std::string routine_name, Routine *routine)
{
    if (routines.count(routine_name))
    {
        throw std::runtime_error("routine \"" + routine_name + "\" already exists");
    }
    routines[routine_name] = routine;
}

void Global::add_variable(std::string variable_name, Variable *variable)
{
    if (variables.count(variable_name))
    {
        throw std::runtime_error("variable \"" + variable_name + "\" already exists");
    }
    variables[variable_name] = variable;
}

void Global::add_rule(Rule *rule)
{
    rules.push_back(rule);
}

bool Global::has_module(std::string module_name)
{
    return modules.count(module_name);
}

bool Global::has_routine(std::string routine_name)
{
    return routines.count(routine_name);
}

bool Global::has_variable(std::string variable_name)
{
    return variables.count(variable_name);
}
