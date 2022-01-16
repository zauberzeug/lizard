#include "method_call.h"

MethodCall::MethodCall(Module *const module, const std::string method_name, const std::vector<Expression_ptr> arguments)
    : module(module), method_name(method_name), arguments(arguments) {
}

MethodCall::~MethodCall() {
    // NOTE: don't delete globally managed modules
}

bool MethodCall::run() {
    this->module->call_with_shadows(this->method_name, this->arguments);
    return true;
}