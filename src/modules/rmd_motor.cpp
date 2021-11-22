#include "rmd_motor.h"

RmdMotor::RmdMotor(std::string name, Can *can) : Module(rmd_motor, name)
{
    this->can = can;
}

void RmdMotor::call(std::string method_name, std::vector<Expression *> arguments)
{
    if (method_name == "zero")
    {
        this->can->send(this->can_id, 0x19, 0, 0, 0, 0, 0, 0, 0);
    }
    else
    {
        Module::call(method_name, arguments);
    }
}
