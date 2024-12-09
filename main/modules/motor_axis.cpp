#include "motor_axis.h"
#include "utils/uart.h"
#include <stdexcept>

const std::map<std::string, Variable_ptr> MotorAxis::get_defaults() {
    return {};
}

MotorAxis::MotorAxis(const std::string name, const Motor_ptr motor, const Input_ptr input1, const Input_ptr input2)
    : Module(motor_axis, name), motor(motor), input1(input1), input2(input2) {
    this->merge_properties(MotorAxis::get_defaults());
}

bool MotorAxis::can_move(const float speed) const {
    if (speed < 0 && this->input1->get_property("active")->boolean_value) {
        return false;
    }
    if (speed > 0 && this->input2->get_property("active")->boolean_value) {
        return false;
    }
    return true;
}

void MotorAxis::step() {
    float speed = this->motor->get_speed();
    if (!this->can_move(speed)) {
        this->motor->stop();
    }
    Module::step();
}

void MotorAxis::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "position") {
        if (arguments.size() < 2 || arguments.size() > 3) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery, numbery);
        // Check distance because speed is always positive for ODriveMotors in position mode
        float distance = arguments[0]->evaluate_number() - this->motor->get_position();
        if (this->can_move(distance)) {
            this->motor->position(arguments[0]->evaluate_number(), arguments[1]->evaluate_number(), arguments.size() > 2 ? std::abs(arguments[2]->evaluate_number()) : 0);
        } else {
            this->motor->stop();
        }
    } else if (method_name == "speed") {
        if (arguments.size() < 1 || arguments.size() > 2) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery);
        float speed = arguments[0]->evaluate_number();
        if (this->can_move(speed)) {
            this->motor->speed(speed, arguments.size() > 1 ? std::abs(arguments[1]->evaluate_number()) : 0);
        } else {
            this->motor->stop();
        }
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->motor->stop();
    } else {
        Module::call(method_name, arguments);
    }
}

void MotorAxis::write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) {
    if (property_name == "speed") {
        float speed = expression->evaluate_number();
        if (this->can_move(speed)) {
            this->motor->speed(speed, 0); // Use default acceleration of 0
        } else {
            this->motor->stop();
        }
    } else if (property_name == "stop") {
        this->motor->stop();
    } else {
        Module::write_property(property_name, expression, from_expander);
    }
}
