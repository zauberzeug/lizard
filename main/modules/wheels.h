#pragma once

#include "module.h"

class Wheels;
using Wheels_ptr = std::shared_ptr<Wheels>;

/**
 * Shared base for differential-drive wheels modules.
 *
 * Owns the common properties (`width`, `linear_speed`, `angular_speed`, `enabled`, `drivable`),
 * the enabled-sync in `step()` and the `speed`/`enable`/`disable` command flow. Concrete
 * drivetrains provide the motor-specific parts through the protected hooks.
 *
 * `drivable` is a handbrake: while `false` the motors stay enabled but every drive command
 * brakes to a hold instead of moving, so an external rule can block driving without switching
 * the motors off (that is what `enabled`/`disable` are for). Driving resumes on `drivable = true`.
 */
class Wheels : public Module {
protected:
    /// Value of the `drivable` handbrake property.
    bool is_drivable() const;

    /// Gate shared by all drive commands (`speed` and `power`): returns true if driving is allowed.
    /// If disabled it returns false silently; if not drivable it brakes to a hold and returns false.
    bool gate_or_brake();

    /// Write `linear_speed`/`angular_speed` from measured per-wheel speeds; call from `update_odometry()`.
    void update_speeds(double left_speed, double right_speed);

    /// Apply per-wheel target speeds (already split from linear/angular via `width`).
    virtual void do_wheel_speeds(double left, double right) = 0;
    virtual void do_enable() = 0;
    virtual void do_disable() = 0;
    /// Update `linear_speed`/`angular_speed` from the motors; called every `step()`.
    virtual void update_odometry() = 0;

private:
    bool enabled = true;

public:
    /// Shared property defaults; subclasses that add properties shadow this in their own `get_defaults()`.
    static const std::map<std::string, Variable_ptr> get_defaults();
    Wheels(const std::string name);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander = false) override;
    void enable();
    void disable();
};
