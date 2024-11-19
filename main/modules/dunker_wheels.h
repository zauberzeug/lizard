#pragma once

#include "dunker_motor.h"
#include "module.h"

class DunkerWheels : public Module {
private:
    const DunkerMotor_ptr left_motor;
    const DunkerMotor_ptr right_motor;

public:
    DunkerWheels(const std::string name, const DunkerMotor_ptr left_motor, const DunkerMotor_ptr right_motor);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
};
