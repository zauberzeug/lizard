#pragma once

#include "module.h"
#include "wheels.h"

class Joystick;
using Joystick_ptr = std::shared_ptr<Joystick>;

/**
 * Drives a wheels module with relative joystick values.
 *
 * `drive(forward, turn)` takes values in -1..1. The module ramps towards them (`ramp`), reduces
 * steering at high forward speed (`turn_reduction`), scales by the wheels' maximum linear and
 * angular speed (declared, or derived from the drivetrain's wheel limit), keeps the fastest wheel
 * within that limit and calls `wheels.speed(...)` every cycle while in motion. The wheels'
 * `enabled`, `locked` and dead man's switch apply unchanged. When no `drive()` arrives for
 * `timeout` seconds, the target drops to zero and the ramp brings the robot to a stop. At
 * standstill the zero-speed command is repeated for a moment — a single frame can be lost on the
 * bus or refused by a drive, which would leave a wheel creeping at its last setpoint — and then
 * the module falls silent, so other hosts (e.g. rosys) and the wheels' own dead man's switch are
 * not disturbed.
 */
class Joystick : public Module {
public:
    static inline constexpr const char *TYPE = "Joystick";

    Joystick(const std::string name, const Wheels_ptr wheels);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

private:
    const Wheels_ptr wheels;

    double target_forward = 0.0; // last `drive()` values, clamped to -1..1
    double target_turn = 0.0;
    unsigned long last_drive_millis = 0;                    // `millis()` of the last `drive()`
    unsigned long last_step_micros = 0;                     // for the ramp's time step; 0 until the first `step()`
    static constexpr unsigned int STOP_REPEAT_CYCLES = 100; // keep sending the stop for about a second
    unsigned int stop_cycles_left = 0;                      // zero-speed commands still to send after reaching standstill

    /// Move `value` towards `target` by at most `max_step`.
    static double approach(const double value, const double target, const double max_step);
};
