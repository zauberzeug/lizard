#pragma once

#include <string>
#include "../modules/module.h"
#include "variable.h"

class Expression
{
public:
    virtual double evaluate() = 0;
};

class ConstExpression : public Expression
{
private:
    double value;

public:
    ConstExpression(double value);
    double evaluate();
};

class PropertyGetterExpression : public Expression
{
private:
    Module *module;
    std::string property_name;

public:
    PropertyGetterExpression(Module *module, std::string property_name);
    double evaluate();
};

class VariableGetterExpression : public Expression
{
private:
    Variable *variable;

public:
    VariableGetterExpression(Variable *variable);
    double evaluate();
};
