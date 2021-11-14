#include "expression.h"

ConstExpression::ConstExpression(double value)
{
    this->value = value;
}

double ConstExpression::evaluate()
{
    return this->value;
}

PropertyGetterExpression::PropertyGetterExpression(Module *module, std::string property_name)
{
    this->module = module;
    this->property_name = property_name;
}

double PropertyGetterExpression::evaluate()
{
    return this->module->get(this->property_name);
}