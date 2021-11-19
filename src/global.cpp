#include "global.h"

std::map<std::string, Module *> Global::modules;
std::map<std::string, Routine *> Global::routines;
std::list<Rule *> Global::rules;
std::map<std::string, Variable *> Global::variables;
