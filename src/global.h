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
    static std::list<Rule *> rules;
    static std::map<std::string, Variable *> variables;
};
