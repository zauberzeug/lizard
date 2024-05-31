// TODO: ONLY FOR TESTING PURPOSES, REMOVE BEFORE MERGING
#include "dummy_motor.h"
#include "../utils/uart.h"

DummyMotor::DummyMotor(const std::string name)
    : Module(dummy_motor, name) {
}

void DummyMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "is_running") {
        Module::expect(arguments, 0);
        this->is_running();
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->stop();
    } else if (method_name == "speed") {
        if (arguments.size() == 0) {
            Module::expect(arguments, 0);
            this->speed();
        } else if (arguments.size() == 3) {
            Module::expect(arguments, 3, numbery);
            this->speed(arguments[0]->evaluate_number(), arguments[1]->evaluate_number());
        }
    } else if (method_name == "position") {
        if (arguments.size() == 0) {
            Module::expect(arguments, 0);
            this->position();
        } else if (arguments.size() == 3) {
            Module::expect(arguments, 3, numbery);
            this->position(arguments[0]->evaluate_number(), arguments[1]->evaluate_number(), arguments[2]->evaluate_number());
        }
    } else {
        Module::call(method_name, arguments);
    }
}



bool DummyMotor::is_running() {
    // echo("bool is_running");
    return false;
}

void DummyMotor::stop() {
    // echo("void stop");
    return;
}

double DummyMotor::position() {
    // echo("double position");
    return 0.0;
}

void DummyMotor::position(const double position, const double speed, const uint32_t acceleration) {
    // echo("void position");
    return;
}

double DummyMotor::speed() {
    // echo("double speed");
    return 0.0;
}

void DummyMotor::speed(const double speed, const uint32_t acceleration) {
    // echo("void speed");
    return;
}