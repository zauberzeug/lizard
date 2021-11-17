#include "roboclaw_motor.h"

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

RoboClawMotor::RoboClawMotor(std::string name, RoboClaw *roboclaw, unsigned int motor_number) : Module(roboclaw_motor, name)
{
    this->motor_number = constrain(motor_number, 1, 2);
    if (this->motor_number != motor_number)
    {
        printf("error: replacing illegal motor number %d with %d\n", motor_number, this->motor_number);
    }
    this->roboclaw = roboclaw;
}

void RoboClawMotor::call(std::string method, std::vector<Argument *> arguments)
{
    if (method == "power")
    {
        if (arguments.size() != 1 ||
            !arguments[0]->is_numbery())
        {
            printf("error: expecting 1 number argument for method \"%s.%s\"\n", this->name.c_str(), method.c_str());
            return;
        }
        unsigned short int duty = (short int)(constrain(arguments[0]->to_number(), -1, 1) * 32767);
        this->motor_number == 1 ? this->roboclaw->DutyM1(duty) : this->roboclaw->DutyM2(duty);
    }
    else if (method == "speed")
    {
        if (arguments.size() != 1 ||
            !arguments[0]->is_numbery())
        {
            printf("error: expecting 1 number argument for method \"%s.%s\"\n", this->name.c_str(), method.c_str());
            return;
        }
        unsigned int counts_per_second = arguments[0]->to_number();
        this->motor_number == 1 ? this->roboclaw->DutyM1(counts_per_second) : this->roboclaw->DutyM2(counts_per_second);
    }
    else
    {
        Module::call(method, arguments);
    }
}
