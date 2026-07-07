#pragma once

#include "roboclaw_motor.h"
#include "wheels.h"

class RoboClawWheels : public Wheels {
private:
    const RoboClawMotor_ptr left_motor;
    const RoboClawMotor_ptr right_motor;

    unsigned long int last_micros;
    int64_t last_left_position;
    int64_t last_right_position;
    bool initialized = false;

protected:
    void do_wheel_speeds(double left, double right) override;
    void do_enable() override;
    void do_disable() override;
    void update_odometry() override;

public:
    static inline constexpr const char *TYPE = "RoboClawWheels";

    RoboClawWheels(const std::string name, const RoboClawMotor_ptr left_motor, const RoboClawMotor_ptr right_motor);
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
