#include "method_call.h"

MethodCall::MethodCall(Module *module, std::string method_name)
{
    this->module = module;
    this->method_name = method_name;
}

void MethodCall::run()
{
    this->module->call(this->method_name);
}