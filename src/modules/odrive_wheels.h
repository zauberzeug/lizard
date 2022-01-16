#pragma once

#include "module.h"
#include "odrive_motor.h"

class ODriveWheels : public Module {
private:
    ODriveMotor *const left_motor;
    ODriveMotor *const right_motor;

public:
    ODriveWheels(const std::string name, ODriveMotor *const left_motor, ODriveMotor *const right_motor);
    void step();
    void call(const std::string method_name, const std::vector<Expression_ptr> arguments);
};