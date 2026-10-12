#pragma once

#include "module.h"
#include "wheels.h"

class Joystick;
using Joystick_ptr = std::shared_ptr<Joystick>;

/**
 * Drives a wheels module with relative joystick values.
 *
 * `drive(forward, turn)` takes values in -1..1. The module ramps towards them (`ramp`), reduces
 * steering at high forward speed (`turn_reduction`), scales by its `max_linear_speed` and
 * `max_angular_speed` (one may be left at 0 and follows from the other through the wheel
 * distance), keeps the fastest wheel within what either maximum alone needs and calls
 * `wheels.speed(...)` — shadows included — every cycle while in motion. A released stick
 * (`drive(0, 0)`, or no `drive()` for `timeout` seconds or the wheels' shorter
 * `drive_command_timeout`) stops at once, without the ramp. The wheels' `enabled` and `locked`
 * apply unchanged; while they refuse commands the setpoints are held at zero, so the ramp starts
 * fresh once driving is allowed again. At standstill the zero-speed command is repeated for a
 * moment — a single frame can be lost on the bus or refused by a drive, which would leave a wheel
 * creeping at its last setpoint — unless someone else commands the wheels first, and then the
 * module falls silent, so other hosts (e.g. rosys) and the wheels' own dead man's switch are not
 * disturbed.
 */
class Joystick : public Module {
public:
    static inline constexpr const char *TYPE = "Joystick";

    Joystick(const std::string name, const Wheels_ptr wheels);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void write_property(const std::string property_name, const ConstExpression_ptr expression,
                        const bool from_expander = false) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

private:
    static constexpr double DEADBAND = 0.01;                  // `drive()` values below this count as a centred stick
    static constexpr unsigned long STOP_REPEAT_MILLIS = 1000; // keep sending the stop for this long

    const Wheels_ptr wheels;

    double target_forward = 0.0; // last `drive()` values, clamped to -1..1; zeroed once they go stale
    double target_turn = 0.0;
    unsigned long last_drive_millis = 0;  // `millis()` of the last `drive()`
    unsigned long last_step_micros = 0;   // for the ramp's time step; 0 until the first `step()`
    bool was_nonzero = false;             // the setpoints were non-zero in the previous cycle
    bool repeating_stop = false;          // still repeating the zero-speed command after reaching standstill
    unsigned long last_motion_millis = 0; // `millis()` of the last cycle with non-zero setpoints
    unsigned long sent_command_count = 0; // the wheels' command count right after this module's last command
    bool warned_no_maximum = false;       // the "no maximum speed" warning was echoed
};
