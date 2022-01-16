#include "roboclaw_motor.h"
#include <memory>

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

RoboClawMotor::RoboClawMotor(const std::string name, RoboClaw *const roboclaw, const unsigned int motor_number)
    : Module(roboclaw_motor, name), motor_number(constrain(motor_number, 1, 2)), roboclaw(roboclaw) {
    if (this->motor_number != motor_number) {
        throw std::runtime_error("illegal motor number");
    }
    this->properties["position"] = std::make_shared<IntegerVariable>();
}

void RoboClawMotor::step() {
    uint8_t status;
    bool valid;
    int32_t position = this->motor_number == 1 ? this->roboclaw->ReadEncM1(&status, &valid) : this->roboclaw->ReadEncM2(&status, &valid);
    if (!valid) {
        throw std::runtime_error("could not read motor position");
    }
    this->properties["position"]->integer_value = position;
    Module::step();
}

void RoboClawMotor::call(const std::string method_name, const std::vector<Expression_ptr> arguments) {
    if (method_name == "power") {
        Module::expect(arguments, 1, numbery);
        unsigned short int duty = (short int)(constrain(arguments[0]->evaluate_number(), -1, 1) * 32767);
        bool success = this->motor_number == 1 ? this->roboclaw->DutyM1(duty) : this->roboclaw->DutyM2(duty);
        if (!success) {
            throw std::runtime_error("could not set duty cycle");
        }
    } else if (method_name == "speed") {
        Module::expect(arguments, 1, numbery);
        unsigned int counts_per_second = constrain(arguments[0]->evaluate_number(), -32767, 32767);
        bool success = this->motor_number == 1 ? this->roboclaw->DutyM1(counts_per_second) : this->roboclaw->DutyM2(counts_per_second);
        if (!success) {
            throw std::runtime_error("could not set speed");
        }
    } else if (method_name == "zero") {
        bool success = this->motor_number == 1 ? this->roboclaw->SetEncM1(0) : this->roboclaw->SetEncM2(0);
        if (!success) {
            throw std::runtime_error("could not reset position");
        }
    } else {
        Module::call(method_name, arguments);
    }
}
