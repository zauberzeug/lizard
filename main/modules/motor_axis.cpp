#include "motor_axis.h"
#include "utils/uart.h"

MotorAxis::MotorAxis(const std::string name, const Motor_ptr motor, const Input_ptr input1, const Input_ptr input2)
    : Module(motor_axis, name), motor(motor), input1(input1), input2(input2) {
}

void MotorAxis::check_inputs() const {
    try {
        if (this->motor->speed() < 0 && this->input1->get_property("active")->boolean_value) {
            this->motor->stop();
        }
        if (this->motor->speed() > 0 && this->input2->get_property("active")->boolean_value) {
            this->motor->stop();
        }
    } catch (std::runtime_error &e) {
        echo("error in MotorAxis::step(): %s", e.what());
    }
}

void MotorAxis::step() {
    this->check_inputs();
    Module::step();
}

void MotorAxis::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "position") {
        if (arguments.size() < 2 || arguments.size() > 3) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery, numbery);
        // TODO: check before sending a command to the motor
        this->motor->position(arguments[0]->evaluate_number(),
                              arguments[1]->evaluate_number(),
                              arguments.size() > 2 ? std::abs(arguments[2]->evaluate_number()) : 0);
        this->check_inputs();
    } else if (method_name == "speed") {
        if (arguments.size() < 1 || arguments.size() > 2) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery);
        this->motor->speed(arguments[0]->evaluate_number(),
                           arguments.size() > 1 ? std::abs(arguments[1]->evaluate_number()) : 0);
        this->check_inputs();
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->motor->stop();
    } else {
        Module::call(method_name, arguments);
    }
}
