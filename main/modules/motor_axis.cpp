#include "motor_axis.h"

MotorAxis::MotorAxis(const std::string name, const StepperMotor_ptr motor, const Input_ptr input1, const Input_ptr input2)
    : Module(motor_axis, name), motor(motor), input1(input1), input2(input2) {
}

void MotorAxis::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "reference") {
        Module::expect(arguments, 0);
        // TODO
    } else {
        Module::call(method_name, arguments);
    }
}
