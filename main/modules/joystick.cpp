#include "joystick.h"
#include "../compilation/expressions.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "module_helpers.h"
#include <algorithm>
#include <cmath>

static Module_ptr create_joystick(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    Module::expect(arguments, 1, identifier);
    const Wheels_ptr wheels = get_module_argument<Wheels>(arguments[0]);
    return std::make_shared<Joystick>(name, wheels);
}
REGISTER_MODULE(Joystick, &create_joystick)

const std::map<std::string, Variable_ptr> Joystick::get_defaults() {
    return {
        {"max_linear_speed", std::make_shared<NumberVariable>(0.0)},
        {"max_angular_speed", std::make_shared<NumberVariable>(0.0)},
        {"ramp", std::make_shared<NumberVariable>(2.0)},
        {"turn_reduction", std::make_shared<NumberVariable>(0.5)},
        {"timeout", std::make_shared<NumberVariable>(1.0)},
        {"forward", std::make_shared<NumberVariable>()},
        {"turn", std::make_shared<NumberVariable>()},
        {"active", std::make_shared<BooleanVariable>(false)},
    };
}

Joystick::Joystick(const std::string name, const Wheels_ptr wheels)
    : Module(name), wheels(wheels) {
    this->properties = Joystick::get_defaults();
}

void Joystick::step() {
    // Time since the last cycle for the ramp; the first cycle has no history and does not move.
    const double dt = this->last_step_micros ? micros_since(this->last_step_micros) / 1e6 : 0.0;
    this->last_step_micros = micros();

    // Dead man's switch: once drive() went stale (the remote fell silent), the stick counts as released. The
    // wheels' own switch is fed by this module while in motion, so their shorter timeout is honoured here as well.
    double timeout = this->properties.at("timeout")->number_value();
    const double wheels_timeout = this->wheels->get_property("drive_command_timeout")->number_value();
    if (wheels_timeout > 0.0) {
        timeout = std::min(timeout, wheels_timeout);
    }
    if (millis_since(this->last_drive_millis) > timeout * 1000.0) {
        this->target_forward = 0.0;
        this->target_turn = 0.0;
    }

    // Maxima: a missing one follows from the other through the wheel distance; without either the module cannot
    // drive, says so once and stays silent.
    const double width = this->wheels->get_property("width")->number_value();
    double max_linear = this->properties.at("max_linear_speed")->number_value();
    double max_angular = this->properties.at("max_angular_speed")->number_value();
    if (width > 0.0 && max_linear <= 0.0) {
        max_linear = max_angular * width / 2.0;
    }
    if (width > 0.0 && max_angular <= 0.0) {
        max_angular = 2.0 * max_linear / width;
    }
    const bool released = this->target_forward == 0.0 && this->target_turn == 0.0;
    const bool has_maximum = max_linear > 0.0 && max_angular > 0.0;
    if (has_maximum) {
        this->warned_no_maximum = false;
    } else if (!released && !this->warned_no_maximum) {
        echo("warning: joystick %s cannot drive: neither max_linear_speed nor max_angular_speed is set",
             this->name.c_str());
        this->warned_no_maximum = true;
    }

    // Ramp: limit the change of the normalized setpoints per second; 0 applies the target directly. A released
    // stick (zero target, by drive(0, 0) or the timeout) stops at once instead of ramping down. While the wheels
    // refuse commands (disabled or locked) the setpoints are held at zero as well, so the ramp does not sit at
    // full deflection behind the interlock and jump the robot to full speed the moment it lifts.
    const double ramp = this->properties.at("ramp")->number_value();
    double forward = std::clamp(this->properties.at("forward")->number_value(), -1.0, 1.0);
    double turn = std::clamp(this->properties.at("turn")->number_value(), -1.0, 1.0);
    if (released || !has_maximum || !this->wheels->may_drive()) {
        forward = 0.0;
        turn = 0.0;
    } else if (ramp > 0.0) {
        forward = std::clamp(this->target_forward, forward - ramp * dt, forward + ramp * dt);
        turn = std::clamp(this->target_turn, turn - ramp * dt, turn + ramp * dt);
    } else {
        forward = this->target_forward;
        turn = this->target_turn;
    }
    this->properties.at("forward")->set_number_value(forward);
    this->properties.at("turn")->set_number_value(turn);

    // Drive the wheels every cycle while in motion (this also feeds their dead man's switch). When the
    // ramp reaches standstill, repeat the stop for a while: a single frame can be lost on the bus or
    // refused by a drive (seen on Innotronic tracks, which then crept on at the last setpoint). The repeat
    // ends early as soon as someone else commands the wheels (rosys, a rule, `off()`), so it does not
    // overwrite them. Then stay silent.
    const bool nonzero = forward != 0.0 || turn != 0.0;
    this->properties.at("active")->set_boolean_value(nonzero);
    if (nonzero) {
        this->repeating_stop = true;
        this->last_motion_millis = millis();
    } else if (this->repeating_stop && !this->was_nonzero) {
        const bool overtaken = this->wheels->get_command_count() != this->sent_command_count;
        if (overtaken || millis_since(this->last_motion_millis) >= STOP_REPEAT_MILLIS) {
            this->repeating_stop = false;
        }
    }
    this->was_nonzero = nonzero;
    if (!nonzero && !this->repeating_stop) {
        Module::step();
        return;
    }

    // Steering reduction: full turn rate at standstill, `turn_reduction` of it at full forward speed.
    const double turn_reduction = std::clamp(this->properties.at("turn_reduction")->number_value(), 0.0, 1.0);
    const double turn_factor = 1.0 - (1.0 - turn_reduction) * std::abs(forward);
    double linear = forward * max_linear;
    double angular = turn * turn_factor * max_angular;
    // Saturation guard: driving and turning at once asks the outer wheel for more than linear or angular alone.
    // Keep it within the wheel speed that full linear or full angular speed alone needs, scaling both down
    // together, so the robot goes where the stick points instead of veering when a drive cuts that wheel off.
    if (width > 0.0) {
        const double wheel_limit = std::max(max_linear, max_angular * width / 2.0);
        const double fastest_wheel = std::abs(linear) + std::abs(angular) * width / 2.0;
        if (fastest_wheel > wheel_limit) {
            linear *= wheel_limit / fastest_wheel;
            angular *= wheel_limit / fastest_wheel;
        }
    }
    // Through the wheels' command like a script, so shadowed wheels are driven along.
    this->wheels->call_with_shadows("speed", {std::make_shared<NumberExpression>(linear),
                                              std::make_shared<NumberExpression>(angular)});
    this->sent_command_count = this->wheels->get_command_count();

    Module::step();
}

void Joystick::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "drive") {
        Module::expect(arguments, 2, numbery, numbery);
        const double forward = arguments[0]->evaluate_number();
        const double turn = arguments[1]->evaluate_number();
        if (!std::isfinite(forward) || !std::isfinite(turn)) {
            this->target_forward = 0.0;
            this->target_turn = 0.0;
            throw std::runtime_error("joystick \"" + this->name + "\" got a non-finite drive() value and stopped");
        }
        // A small deadband, so a remote whose centre is not exactly zero still releases the stick.
        this->target_forward = std::abs(forward) < DEADBAND ? 0.0 : std::clamp(forward, -1.0, 1.0);
        this->target_turn = std::abs(turn) < DEADBAND ? 0.0 : std::clamp(turn, -1.0, 1.0);
        this->last_drive_millis = millis();
    } else {
        Module::call(method_name, arguments);
    }
}

void Joystick::write_property(const std::string property_name, const ConstExpression_ptr expression,
                              const bool from_expander) {
    if (property_name == "timeout" && !(expression->evaluate_number() > 0.0)) {
        throw std::runtime_error("joystick timeout must be positive; it is the only dead man's switch while driving");
    }
    if ((property_name == "ramp" || property_name == "max_linear_speed" || property_name == "max_angular_speed") &&
        !(expression->evaluate_number() >= 0.0)) {
        throw std::runtime_error("joystick " + property_name + " must not be negative");
    }
    Module::write_property(property_name, expression, from_expander);
}
