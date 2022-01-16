#pragma once

#include "compilation/routine.h"
#include "compilation/rule.h"
#include "compilation/variable.h"
#include "modules/module.h"
#include <list>
#include <map>
#include <string>

class Global {
public:
    static std::map<const std::string, Module *> modules;
    static std::map<const std::string, Routine *> routines;
    static std::map<const std::string, Variable_ptr> variables;
    static std::list<Rule *> rules;

    static Module *get_module(const std::string module_name);
    static Routine *get_routine(const std::string routine_name);
    static Variable_ptr get_variable(const std::string variable_name);

    static void add_module(const std::string module_name, Module *const module);
    static void add_routine(const std::string routine_name, Routine *const routine);
    static void add_variable(const std::string variable_name, const Variable_ptr variable);
    static void add_rule(Rule *rule);

    static bool has_module(const std::string module_name);
    static bool has_routine(const std::string routine_name);
    static bool has_variable(const std::string variable_name);
};
