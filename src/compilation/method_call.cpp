#include "method_call.h"

MethodCall::MethodCall(const Module_ptr module, const std::string method_name, const std::vector<Expression_ptr> arguments)
    : module(module), method_name(method_name), arguments(arguments) {
}

bool MethodCall::run() {
    this->module->call_with_shadows(this->method_name, this->arguments);
    return true;
}