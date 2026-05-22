#pragma once

#include "module.h"
#include "roboclaw.h"

class RoboClawMotor;
using RoboClawMotor_ptr = std::shared_ptr<RoboClawMotor>;

class RoboClawMotor : public Module {
private:
    static constexpr uint32_t DEFAULT_SPEED = 5000;
    static constexpr uint32_t DEFAULT_ACCEL = 20000;
    static constexpr uint32_t DEFAULT_DECCEL = 20000;

    const unsigned int motor_number;
    const RoboClaw_ptr roboclaw;
    bool enabled = true;
    bool calibrated = false;

public:
    RoboClawMotor(const std::string name, const RoboClaw_ptr roboclaw, const unsigned int motor_number);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

    void enable();
    void disable();
    int64_t get_position() const;
    void power(double value);
    void speed(int value);
    void stop();
    void set_limits(int32_t low, int32_t high);
    bool is_calibrated() const;
    int32_t get_limit_low() const;
    int32_t get_limit_high() const;
    void position(int32_t target, uint32_t speed, uint32_t accel, uint32_t deccel);
};
