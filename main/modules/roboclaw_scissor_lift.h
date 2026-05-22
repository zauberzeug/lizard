#pragma once

#include "roboclaw_motor.h"

class RoboClawScissorLift : public Module {
private:
    static constexpr uint32_t DEFAULT_SPEED = 5000;
    static constexpr uint32_t DEFAULT_ACCEL = 20000;
    static constexpr uint32_t DEFAULT_DECCEL = 20000;

    const RoboClawMotor_ptr motor1;
    const RoboClawMotor_ptr motor2;
    bool enabled = true;

    void enable();
    void disable();
    void drive(double percent, uint32_t speed, uint32_t accel, uint32_t deccel);

public:
    RoboClawScissorLift(const std::string name, const RoboClawMotor_ptr motor1, const RoboClawMotor_ptr motor2);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
