#pragma once

#include "module.h"
#include "odrive_motor.h"

class ODriveWheels : public Module {
private:
    const ODriveMotor_ptr left_motor;
    const ODriveMotor_ptr right_motor;

public:
    ODriveWheels(const std::string name, const ODriveMotor_ptr left_motor, const ODriveMotor_ptr right_motor);
    void step();
    void call(const std::string method_name, const std::vector<Expression_ptr> arguments);
};