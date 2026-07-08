#pragma once

#include "module.h"

class Wheels;
using Wheels_ptr = std::shared_ptr<Wheels>;

/**
 * Shared base for differential-drive wheels modules.
 *
 * Owns the common properties (`width`, `linear_speed`, `angular_speed`, `enabled`, `locked`),
 * the enabled-sync in `step()` and the `speed`/`enable`/`disable` command flow. Concrete
 * drivetrains provide the motor-specific parts through the protected hooks.
 *
 * `locked` is a safety interlock: while `true`, drive commands are ignored and the wheels are
 * actively held at standstill (zero-speed setpoint, motors stay enabled), so a rule can block
 * driving while some other condition is unmet — e.g. a tool is not in its parking position —
 * without switching the motors off. The hold is sent on the rising edge of `locked` and
 * refreshed at a low rate. `disable()` and `off()` still switch the motors off; `off()` also
 * suspends the hold until `enable()` is called or `locked` changes. Driving resumes on
 * `locked = false`.
 */
class Wheels : public Module {
private:
    static constexpr unsigned int HOLD_REFRESH_CYCLES = 100; // re-send the standstill hold about once per second

    bool last_applied_enabled = true; // last value synced to the motors; `step()` edge-detects direct property writes against it
    bool holding = false;             // wheels are currently held at standstill by the `locked` interlock
    bool hold_suspended = false;      // `off()` stood the hold down; re-armed by `enable()` or a `locked` change
    unsigned int hold_cycle = 0;      // `step()` cycles since the hold was last sent

    /// Copy the gate properties (`locked`, `enabled`) from this module onto a freshly attached shadow.
    void sync_gate_properties(Module &shadow) const;

protected:
    /// Whether drive commands may be applied: true only while enabled and not locked.
    bool may_drive() const;

    /// Stand the standstill hold down until `enable()` or a `locked` change; call from `off()` handlers.
    void suspend_hold();

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
    void write_property(const std::string property_name, const ConstExpression_ptr expression,
                        const bool from_expander = false) override;
    void enable();
    void disable();
    /// Shared property defaults; subclasses that add properties shadow this and pass the result to the constructor.
    static const std::map<std::string, Variable_ptr> get_defaults();
};
