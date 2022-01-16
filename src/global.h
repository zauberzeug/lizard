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
    static std::map<const std::string, Module_ptr> modules;
    static std::map<const std::string, Routine_ptr> routines;
    static std::map<const std::string, Variable_ptr> variables;
    static std::list<Rule_ptr> rules;

    static Module_ptr get_module(const std::string module_name);
    static Routine_ptr get_routine(const std::string routine_name);
    static Variable_ptr get_variable(const std::string variable_name);

    static void add_module(const std::string module_name, const Module_ptr module);
    static void add_routine(const std::string routine_name, const Routine_ptr routine);
    static void add_variable(const std::string variable_name, const Variable_ptr variable);
    static void add_rule(Rule_ptr rule);

    static bool has_module(const std::string module_name);
    static bool has_routine(const std::string routine_name);
    static bool has_variable(const std::string variable_name);
};
