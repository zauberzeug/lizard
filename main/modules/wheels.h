#pragma once

#include "module.h"

class Wheels;
using Wheels_ptr = std::shared_ptr<Wheels>;

/**
 * Shared base for differential-drive wheels modules.
 *
 * Owns the common properties (`width`, `linear_speed`, `angular_speed`, `enabled`, `locked`,
 * `drive_command_age`, `drive_command_timeout`) and the `speed`/`power`/`off`/`enable`/`disable`
 * command flow; the enabled-sync itself comes from `Module`. Concrete drivetrains provide the
 * motor-specific parts through the protected hooks.
 *
 * `locked` is a safety interlock: while `true`, drive commands are ignored and the wheels are
 * actively held at standstill (zero-speed setpoint, motors stay enabled), so a rule can block
 * driving while some other condition is unmet — e.g. a tool is not in its parking position —
 * without switching the motors off. The hold is sent on the rising edge of `locked` and
 * refreshed at a low rate. While locked, `off()` does not stick — the hold re-engages within
 * about a second; only `disable()` switches the motors off durably. Driving resumes on
 * `locked = false`.
 *
 * `drive_command_timeout` is a dead man's switch: after a non-zero `speed()` or `power()` the
 * wheels stop on their own once no further drive command arrived for that long. Only `speed()`
 * and `power()` count as drive commands, so `enable()`, `off()` or property writes cannot keep a
 * stale motion alive. The stop is sent once; the next drive command re-arms the switch.
 * `drive_command_age` exposes the time since the last drive command for rules.
 */
class Wheels : public Module {
private:
    static constexpr unsigned int HOLD_REFRESH_CYCLES = 100; // re-send the standstill hold about once per second

    bool holding = false;        // wheels are currently held at standstill by the `locked` interlock
    unsigned int hold_cycle = 0; // `step()` cycles since the hold was last sent

    bool moving = false;                         // last applied drive command was non-zero and no stop followed since
    unsigned long last_drive_command_millis = 0; // `millis()` of the last `speed()`/`power()`, applied or not

    /// Record a drive command: refresh `drive_command_age` and, if it was applied, (dis)arm the dead man's switch.
    void note_drive_command(bool applied, bool nonzero);

    /// Copy the shared properties (`locked`, `enabled`, `drive_command_timeout`) onto a freshly attached shadow.
    void sync_shared_properties(Module &shadow) const;

protected:
    /// Whether drive commands may be applied: true only while enabled and not locked.
    bool may_drive() const;

    /// Write `linear_speed`/`angular_speed` from measured per-wheel speeds; call from `update_odometry()`.
    void update_speeds(double left_speed, double right_speed);

    /// Apply per-wheel target speeds (already split from linear/angular via `width`).
    virtual void do_wheel_speeds(double left, double right) = 0;
    /// Apply per-wheel torques; the default rejects `power()` for drivetrains without torque control.
    virtual void do_wheel_powers(double left, double right);
    /// Switch both motors to their idle state; the default rejects `off()` for drivetrains without one.
    virtual void do_off();
    void do_enable() override = 0;
    void do_disable() override = 0;
    /// Update `linear_speed`/`angular_speed` from the motors; called every `step()`.
    virtual void update_odometry() = 0;

public:
    Wheels(const std::string name, const std::map<std::string, Variable_ptr> &defaults = Wheels::get_defaults());
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void write_property(const std::string property_name, const ConstExpression_ptr expression,
                        const bool from_expander = false) override;
    void disable() override;
    /// Shared property defaults; subclasses that add properties shadow this and pass the result to the constructor.
    static const std::map<std::string, Variable_ptr> get_defaults();
};
