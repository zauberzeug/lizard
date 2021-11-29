#include "method_call.h"

MethodCall::MethodCall(Module *module, std::string method_name, std::vector<Expression *> arguments)
{
    this->module = module;
    this->method_name = method_name;
    this->arguments = arguments;
}

MethodCall::~MethodCall()
{
    // NOTE: don't delete globally managed modules
    for (auto a : this->arguments)
    {
        delete a;
    }
    this->arguments.clear();
}

bool MethodCall::run()
{
    this->module->call_with_shadows(this->method_name, this->arguments);
    return true;
}