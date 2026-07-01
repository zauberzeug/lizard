#pragma once

#include "dunker_motor.h"
#include "wheels.h"

class DunkerWheels : public Wheels {
private:
    const DunkerMotor_ptr left_motor;
    const DunkerMotor_ptr right_motor;

protected:
    void do_wheel_speeds(double left, double right) override;
    void do_enable() override;
    void do_disable() override;
    void update_odometry() override;

public:
    static inline constexpr const char *TYPE = "DunkerWheels";

    DunkerWheels(const std::string name, const DunkerMotor_ptr left_motor, const DunkerMotor_ptr right_motor);
    static const std::map<std::string, Variable_ptr> get_defaults();
};
