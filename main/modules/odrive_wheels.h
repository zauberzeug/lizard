#pragma once

#include "module.h"
#include "odrive_motor.h"

class ODriveWheels : public Module {
private:
    const ODriveMotor_ptr left_motor;
    const ODriveMotor_ptr right_motor;

    bool initialized = false;
    unsigned long int last_micros;
    double last_left_position;
    double last_right_position;

public:
    ODriveWheels(const std::string name, const ODriveMotor_ptr left_motor, const ODriveMotor_ptr right_motor);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
