#include "rule.h"

Rule::Rule(const Expression_ptr condition, const Routine_ptr routine)
    : condition(condition), routine(routine) {
}
