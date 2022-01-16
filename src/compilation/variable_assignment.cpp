#include "variable_assignment.h"

VariableAssignment::VariableAssignment(const Variable_ptr variable, const Expression_ptr expression)
    : variable(variable), expression(expression) {
}

VariableAssignment::~VariableAssignment() {
    // NOTE: don't delete globally managed variables
}

bool VariableAssignment::run() {
    this->variable->assign(this->expression);
    return true;
}