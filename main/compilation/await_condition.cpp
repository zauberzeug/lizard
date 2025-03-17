#include "await_condition.h"

AwaitCondition::AwaitCondition(const ConstExpression_ptr condition)
    : condition(condition) {
}

bool AwaitCondition::run() {
    return this->condition->evaluate_boolean();
}
