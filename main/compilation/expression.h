#pragma once

#include "type.h"
#include "variable.h"
#include <memory>
#include <string>
#include <vector>

class Expression;
using Expression_ptr = std::shared_ptr<Expression>;
using ConstExpression_ptr = std::shared_ptr<const Expression>;

int write_arguments_to_buffer(const std::vector<ConstExpression_ptr> arguments, char *buffer, size_t buffer_len);

class Expression {
protected:
    Expression(const Type type);

public:
    const Type type;

    virtual bool evaluate_boolean() const;
    virtual int64_t evaluate_integer() const;
    virtual double evaluate_number() const;
    virtual std::string evaluate_string() const;
    virtual std::string evaluate_identifier() const;
    bool is_numbery() const;
    int print_to_buffer(char *buffer, size_t buffer_len) const;
};
