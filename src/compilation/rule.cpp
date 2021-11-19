#include "rule.h"

Rule::Rule(Expression *condition, Routine *routine)
{
    this->condition = condition;
    this->routine = routine;
}
