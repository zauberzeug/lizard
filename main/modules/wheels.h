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
 * `drivable` is a handbrake: while `false` the motors stay enabled but hold at a stop — drive
 * commands are blocked and `step()` keeps braking to zero every cycle, so the hold engages even
 * when no command arrives. It lets an external rule block driving without switching the motors off
 * (that is what `enabled`/`disable` are for). Driving resumes on `drivable = true`.
 */
class Wheels : public Module {
private:
    bool last_applied_enabled = true; // last value synced to the motors; `step()` edge-detects direct property writes against it

protected:
    /// Whether drive commands may be applied: true only while enabled and drivable.
    bool may_drive() const;

    /// Write `linear_speed`/`angular_speed` from measured per-wheel speeds; call from `update_odometry()`.
    void update_speeds(double left_speed, double right_speed);

    /// Apply per-wheel target speeds (already split from linear/angular via `width`).
    virtual void do_wheel_speeds(double left, double right) = 0;
    virtual void do_enable() = 0;
    virtual void do_disable() = 0;
    /// Update `linear_speed`/`angular_speed` from the motors; called every `step()`.
    virtual void update_odometry() = 0;

public:
    Wheels(const std::string name, const std::map<std::string, Variable_ptr> &defaults = Wheels::get_defaults());
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander = false) override;
    void enable();
    void disable();
    /// Shared property defaults; subclasses that add properties shadow this and pass the result to the constructor.
    static const std::map<std::string, Variable_ptr> get_defaults();
};
