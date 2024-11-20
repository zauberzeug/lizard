#include "roboclaw_motor.h"
#include <memory>
#include <stdexcept>

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

const std::map<std::string, Variable_ptr> &RoboClawMotor::get_defaults() {
    static const std::map<std::string, Variable_ptr> defaults = {
        {"position", std::make_shared<IntegerVariable>()},
    };
    return defaults;
}

RoboClawMotor::RoboClawMotor(const std::string name, const RoboClaw_ptr roboclaw, const unsigned int motor_number)
    : Module(roboclaw_motor, name), motor_number(constrain(motor_number, 1, 2)), roboclaw(roboclaw) {
    if (this->motor_number != motor_number) {
        throw std::runtime_error("illegal motor number");
    }
    this->properties = RoboClawMotor::get_defaults();
}

void RoboClawMotor::step() {
    uint8_t status;
    bool valid;
    int64_t position = this->motor_number == 1 ? this->roboclaw->ReadEncM1(&status, &valid) : this->roboclaw->ReadEncM2(&status, &valid);
    if (!valid) {
        throw std::runtime_error("could not read motor position");
    }
    this->properties["position"]->integer_value = position;
    Module::step();
}

void RoboClawMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "power") {
        Module::expect(arguments, 1, numbery);
        this->power(arguments[0]->evaluate_number());
    } else if (method_name == "speed") {
        Module::expect(arguments, 1, numbery);
        this->speed(arguments[0]->evaluate_number());
    } else if (method_name == "zero") {
        bool success = this->motor_number == 1 ? this->roboclaw->SetEncM1(0) : this->roboclaw->SetEncM2(0);
        if (!success) {
            throw std::runtime_error("could not reset position");
        }
    } else {
        Module::call(method_name, arguments);
    }
}

int64_t RoboClawMotor::get_position() const {
    return this->properties.at("position")->integer_value;
}

void RoboClawMotor::power(double value) {
    unsigned short int duty = (short int)(constrain(value, -1, 1) * 32767);

    const int max_retries = 4;
    for (int retries = 0; retries < max_retries; ++retries) {
        bool success = this->motor_number == 1 ? this->roboclaw->DutyM1(duty) : this->roboclaw->DutyM2(duty);
        if (success) {
            return;
        }
    }
    throw std::runtime_error("could not set duty cycle after " + std::to_string(max_retries) + " retries");
}

void RoboClawMotor::speed(int value) {
    unsigned int counts_per_second = constrain(value, -2147483647, 2147483647);

    const int max_retries = 4;
    for (int retries = 0; retries < max_retries; ++retries) {
        bool success = this->motor_number == 1 ? this->roboclaw->SpeedM1(counts_per_second) : this->roboclaw->SpeedM2(counts_per_second);
        if (success) {
            return;
        }
    }
    throw std::runtime_error("could not set speed after " + std::to_string(max_retries) + " retries");
}
