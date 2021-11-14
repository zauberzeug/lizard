#include "rule.h"

Rule::Rule(Condition *condition, Routine *routine)
{
    this->condition = condition;
    this->routine = routine;
}
