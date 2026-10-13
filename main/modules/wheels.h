#pragma once

#include "module.h"

class Wheels;
using Wheels_ptr = std::shared_ptr<Wheels>;

/**
 * Shared base for differential-drive wheels modules.
 *
 * Owns the common properties (`width`, `max_wheel_speed`, `linear_speed`, `angular_speed`, `enabled`,
 * `locked`, `drive_command_age`, `drive_command_timeout`) and the `speed`/`power`/`off`/`enable`/`disable`
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
 * stale motion alive. A tripped switch holds the wheels at standstill like `locked` does (the
 * zero-speed setpoint is refreshed at a low rate, so a stop that did not reach the motors is
 * re-asserted); the next drive command releases the hold and re-arms the switch.
 * `drive_command_age` exposes the time since the last drive command for rules.
 *
 * `max_wheel_speed` caps `speed()`: when the faster wheel would exceed it, linear and angular
 * speed are scaled down together, so the robot keeps its direction instead of veering when a
 * drive cuts off only the outer wheel. A zero `speed()` after motion is repeated about every
 * 100 ms for a second, since a single stop can be lost on the bus or refused by a drive; a further
 * stop restarts the repeat, any other wheels command ends it.
 */
class Wheels : public Module {
private:
    static constexpr unsigned int HOLD_REFRESH_CYCLES = 100;  // re-send the standstill hold about once per second
    static constexpr unsigned long STOP_REPEAT_MILLIS = 1000; // repeat a stop after motion for this long
    static constexpr unsigned int STOP_REPEAT_CYCLES = 10;    // re-send the repeated stop about every 100 ms

    bool holding = false;        // wheels are currently held at standstill (by `locked` or a tripped dead man's switch)
    unsigned int hold_cycle = 0; // `step()` cycles since the hold was last sent

    bool moving = false;                         // last applied drive command was non-zero and no stop followed since
    bool stopped = false;                        // the dead man's switch tripped and no drive command released it yet
    unsigned long last_drive_command_millis = 0; // `millis()` of the last `speed()`/`power()`, applied or not

    bool repeating_stop = false;          // a zero `speed()` after motion is being repeated
    unsigned long stop_repeat_millis = 0; // `millis()` when that stop was commanded
    unsigned int stop_repeat_cycle = 0;   // `step()` cycles since the stop was last sent

    /// Copy the shared properties (`locked`, `enabled`, `drive_command_timeout`, `max_wheel_speed`) onto a new shadow.
    void sync_shared_properties(Module &shadow) const;

protected:
    /// Whether drive commands may be applied: true only while enabled and not locked.
    bool may_drive() const;

    /// Record an incoming drive command *before* it is sent: refreshes `drive_command_age` and, if the command
    /// is applied and non-zero, arms the dead man's switch — before the send, so a send that fails is still stopped.
    void note_drive_command(bool applied, bool nonzero);
    /// Record that a drive command reached the motors: releases a tripped switch's hold and, for a zero command,
    /// disarms the switch. Drivetrains with their own stop or position commands call both to keep it consistent.
    void note_drive_command_sent(bool nonzero);

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
