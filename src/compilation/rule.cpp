#include "rule.h"

Rule::Rule(const Expression_ptr condition, Routine *const routine)
    : condition(condition), routine(routine) {
}
