#include "method_call.h"

MethodCall::MethodCall(Module *module, std::string method_name, std::vector<Argument *> arguments)
{
    this->module = module;
    this->method_name = method_name;
    this->arguments = arguments;
}

bool MethodCall::run()
{
    this->module->call_with_shadows(this->method_name, this->arguments);
    return true;
}