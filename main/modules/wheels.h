#pragma once

#include "module.h"

class Wheels;
using Wheels_ptr = std::shared_ptr<Wheels>;

/**
 * Shared base for differential-drive wheels modules.
 *
 * Owns the common properties (`width`, `linear_speed`, `angular_speed`, `enabled`, `drivable`),
 * the enabled-sync in `step()` and the `speed`/`enable`/`disable` command flow. Concrete
 * drivetrains provide the motor-specific parts through the protected hooks. `drivable` is a gate
 * that external rules can toggle to (dis)allow driving without disabling the motors.
 */
class Wheels : public Module {
protected:
    bool enabled = true;

    /// Common property defaults; concrete modules extend this in their own `get_defaults()`.
    static std::map<std::string, Variable_ptr> get_wheels_defaults();

    /// True while drive commands are allowed (both `enabled` and the `drivable` property set).
    bool can_drive() const;

    /// Apply per-wheel target speeds (already split from linear/angular via `width`).
    virtual void do_wheel_speeds(double left, double right) = 0;
    virtual void do_enable() = 0;
    virtual void do_disable() = 0;
    /// Update `linear_speed`/`angular_speed` from the motors; called every `step()`.
    virtual void update_odometry() = 0;

public:
    Wheels(const std::string name);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander = false) override;
    void enable();
    void disable();
};
