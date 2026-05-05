#pragma once

#include "dual_drive_motor.h"
#include "module.h"

class DualDriveWheels : public Module {
private:
    const DualDriveMotor_ptr left_motor;
    const DualDriveMotor_ptr right_motor;
    bool last_applied_enabled = true;

    bool is_enabled() const;

public:
    DualDriveWheels(const std::string name, const DualDriveMotor_ptr left_motor, const DualDriveMotor_ptr right_motor);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
    void enable();
    void disable();
};
