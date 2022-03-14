#pragma once

#include "../modules/module.h"
#include "action.h"
#include "expression.h"

class PropertyAssignment : public Action {
public:
    const Module_ptr module;
    const std::string property_name;
    const ConstExpression_ptr expression;

    PropertyAssignment(const Module_ptr module, const std::string property_name, const ConstExpression_ptr expression);
    bool run() override;
};