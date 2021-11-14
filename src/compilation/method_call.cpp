#include "method_call.h"

MethodCall::MethodCall(Module *module, std::string method_name, std::vector<double> arguments)
{
    this->module = module;
    this->method_name = method_name;
    this->arguments = arguments;
}

void MethodCall::run()
{
    this->module->call(this->method_name, this->arguments);
}