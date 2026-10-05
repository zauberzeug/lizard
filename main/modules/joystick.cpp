#include "joystick.h"
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

double Joystick::approach(const double value, const double target, const double max_step) {
    // Snap when within reach, so the target (in particular zero) is hit exactly and not approached asymptotically.
    if (std::abs(target - value) <= max_step) {
        return target;
    }
    return target > value ? value + max_step : value - max_step;
}

void Joystick::step() {
    // Time since the last cycle for the ramp; the first cycle has no history and does not move.
    const double dt = this->last_step_micros ? micros_since(this->last_step_micros) / 1e6 : 0.0;
    this->last_step_micros = micros();

    // Target: the last drive() values, or zero once they went stale. The ramp then brings the robot
    // to a gentle stop instead of the wheels' dead man's switch stopping it hard.
    double target_forward = this->target_forward;
    double target_turn = this->target_turn;
    const double timeout = this->properties.at("timeout")->number_value();
    if (timeout > 0.0 && millis_since(this->last_drive_millis) > timeout * 1000.0) {
        target_forward = 0.0;
        target_turn = 0.0;
    }

    // Without a maximum the setpoints scale to nothing; say so once instead of driving a robot that stays still.
    const bool no_maximum = this->wheels->max_linear_speed() <= 0.0 && this->wheels->max_angular_speed() <= 0.0;
    if (!no_maximum) {
        this->warned_no_maximum = false;
    } else if ((target_forward != 0.0 || target_turn != 0.0) && !this->warned_no_maximum) {
        echo("warning: joystick %s cannot drive: wheels %s have neither max_linear_speed nor max_angular_speed",
             this->name.c_str(), this->wheels->name.c_str());
        this->warned_no_maximum = true;
    }

    // Ramp: limit the change of the normalized setpoints per second; 0 applies the target directly. While the
    // wheels refuse commands (disabled or locked) the setpoints are held at zero, so the ramp does not sit at full
    // deflection behind the interlock and jump the robot to full speed the moment it lifts.
    const double ramp = this->properties.at("ramp")->number_value();
    double forward = this->properties.at("forward")->number_value();
    double turn = this->properties.at("turn")->number_value();
    if (!this->wheels->may_drive()) {
        forward = 0.0;
        turn = 0.0;
    } else if (ramp > 0.0) {
        forward = Joystick::approach(forward, target_forward, ramp * dt);
        turn = Joystick::approach(turn, target_turn, ramp * dt);
    } else {
        forward = target_forward;
        turn = target_turn;
    }
    this->properties.at("forward")->set_number_value(forward);
    this->properties.at("turn")->set_number_value(turn);

    // Drive the wheels every cycle while in motion (this also feeds their dead man's switch). When the
    // ramp reaches standstill, repeat the stop for a while: a single frame can be lost on the bus or
    // refused by a drive (seen on Innotronic tracks, which then crept on at the last setpoint). Then
    // stay silent.
    const bool nonzero = forward != 0.0 || turn != 0.0;
    this->properties.at("active")->set_boolean_value(nonzero);
    if (nonzero) {
        this->stop_cycles_left = STOP_REPEAT_CYCLES;
    }
    if (nonzero || this->stop_cycles_left > 0) {
        if (!nonzero) {
            this->stop_cycles_left--;
        }
        // Steering reduction: full turn rate at standstill, `turn_reduction` of it at full forward speed.
        const double turn_reduction = std::clamp(this->properties.at("turn_reduction")->number_value(), 0.0, 1.0);
        const double turn_factor = 1.0 - (1.0 - turn_reduction) * std::abs(forward);
        double linear = forward * this->wheels->max_linear_speed();
        double angular = turn * turn_factor * this->wheels->max_angular_speed();
        // Saturation guard: driving and turning at once asks one wheel for more than linear or angular alone.
        // If the drivetrain's wheel limit would cut that wheel off, the robot veers instead of going where the
        // stick points, so scale both down together and keep the direction.
        const double wheel_limit = this->wheels->max_wheel_speed();
        if (wheel_limit > 0.0) {
            const double width = this->wheels->get_property("width")->number_value();
            const double fastest_wheel = std::abs(linear) + std::abs(angular) * width / 2.0;
            if (fastest_wheel > wheel_limit) {
                linear *= wheel_limit / fastest_wheel;
                angular *= wheel_limit / fastest_wheel;
            }
        }
        this->wheels->speed(linear, angular);
    }

    Module::step();
}

void Joystick::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "drive") {
        Module::expect(arguments, 2, numbery, numbery);
        this->target_forward = std::clamp(arguments[0]->evaluate_number(), -1.0, 1.0);
        this->target_turn = std::clamp(arguments[1]->evaluate_number(), -1.0, 1.0);
        this->last_drive_millis = millis();
    } else {
        Module::call(method_name, arguments);
    }
}
