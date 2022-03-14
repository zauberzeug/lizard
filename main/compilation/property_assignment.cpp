#include "property_assignment.h"

PropertyAssignment::PropertyAssignment(const Module_ptr module, const std::string property_name, const ConstExpression_ptr expression)
    : module(module), property_name(property_name), expression(expression) {
}

bool PropertyAssignment::run() {
    this->module->write_property(this->property_name, this->expression);
    return true;
}