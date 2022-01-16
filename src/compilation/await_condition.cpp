#include "await_condition.h"

AwaitCondition::AwaitCondition(const Expression_ptr condition)
    : condition(condition) {
}

AwaitCondition::~AwaitCondition() {
}

bool AwaitCondition::run() {
    return this->condition->evaluate_boolean();
}