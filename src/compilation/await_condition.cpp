#include "await_condition.h"

AwaitCondition::AwaitCondition(const Expression *const condition)
    : condition(condition) {
}

AwaitCondition::~AwaitCondition() {
    delete this->condition;
}

bool AwaitCondition::run() {
    return this->condition->evaluate_boolean();
}