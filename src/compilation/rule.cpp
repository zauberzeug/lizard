#include "rule.h"

Rule::Rule(const Expression *const condition, Routine *const routine)
    : condition(condition), routine(routine) {
}
