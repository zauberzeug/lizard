#pragma once

#include "odrive_motor.h"
#include "wheels.h"

class ODriveWheels : public Wheels {
private:
    const ODriveMotor_ptr left_motor;
    const ODriveMotor_ptr right_motor;

    bool initialized = false;
    unsigned long int last_micros;
    double last_left_position;
    double last_right_position;

protected:
    void do_wheel_speeds(double left, double right) override;
    void do_enable() override;
    void do_disable() override;
    void update_odometry() override;

public:
    static inline constexpr const char *TYPE = "ODriveWheels";

    ODriveWheels(const std::string name, const ODriveMotor_ptr left_motor, const ODriveMotor_ptr right_motor);
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
