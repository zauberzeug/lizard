// TODO: ONLY FOR TESTING PURPOSES, REMOVE BEFORE MERGING
#pragma once

#include "module.h"
#include "motor.h"
#include <memory>

class DummyMotor;
using DummyMotor_ptr = std::shared_ptr<DummyMotor>;

class DummyMotor : public Module, public std::enable_shared_from_this<DummyMotor>, virtual public Motor {
public:
    DummyMotor(const std::string name);

    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;

    bool is_running() override;
    void stop() override;
    double position() override;
    void position(const double position, const double speed, const uint32_t acceleration) override;
    double speed() override;
    void speed(const double speed, const uint32_t acceleration) override;
};