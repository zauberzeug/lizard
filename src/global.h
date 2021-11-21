#pragma once

#include <list>
#include <map>
#include <string>

#include "modules/module.h"
#include "compilation/routine.h"
#include "compilation/rule.h"
#include "compilation/variable.h"

class Global
{
public:
    static std::map<std::string, Module *> modules;
    static std::map<std::string, Routine *> routines;
    static std::map<std::string, Variable *> variables;
    static std::list<Rule *> rules;

    static Module *get_module(std::string module_name);
    static Routine *get_routine(std::string routine_name);
    static Variable *get_variable(std::string variable_name);

    static void add_module(std::string module_name, Module *module);
    static void add_routine(std::string routine_name, Routine *routine);
    static void add_variable(std::string variable_name, Variable *variable);
    static void add_rule(Rule *rule);

    static bool has_module(std::string module_name);
    static bool has_routine(std::string routine_name);
    static bool has_variable(std::string variable_name);
};
