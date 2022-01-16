#include "rule.h"

Rule::Rule(const ConstExpression_ptr condition, const Routine_ptr routine)
    : condition(condition), routine(routine) {
}
