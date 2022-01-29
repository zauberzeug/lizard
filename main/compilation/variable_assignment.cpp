#include "variable_assignment.h"

VariableAssignment::VariableAssignment(const Variable_ptr variable, const ConstExpression_ptr expression)
    : variable(variable), expression(expression) {
}

bool VariableAssignment::run() {
    this->variable->assign(this->expression);
    return true;
}