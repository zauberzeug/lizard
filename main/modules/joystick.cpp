#include "joystick.h"
#include "../utils/timing.h"
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

    // Ramp: limit the change of the normalized setpoints per second; 0 applies the target directly.
    const double ramp = this->properties.at("ramp")->number_value();
    double forward = this->properties.at("forward")->number_value();
    double turn = this->properties.at("turn")->number_value();
    if (ramp > 0.0) {
        forward = Joystick::approach(forward, target_forward, ramp * dt);
        turn = Joystick::approach(turn, target_turn, ramp * dt);
    } else {
        forward = target_forward;
        turn = target_turn;
    }
    this->properties.at("forward")->set_number_value(forward);
    this->properties.at("turn")->set_number_value(turn);

    // Drive the wheels every cycle while in motion (this also feeds their dead man's switch), send
    // the stop once when the ramp reaches standstill, then stay silent.
    const bool nonzero = forward != 0.0 || turn != 0.0;
    this->properties.at("active")->set_boolean_value(nonzero);
    if (nonzero || this->commanding) {
        // Steering reduction: full turn rate at standstill, `turn_reduction` of it at full forward speed.
        const double turn_reduction = this->properties.at("turn_reduction")->number_value();
        const double turn_factor = 1.0 - (1.0 - turn_reduction) * std::abs(forward);
        const double max_linear = this->wheels->get_property("max_linear_speed")->number_value();
        const double max_angular = this->wheels->get_property("max_angular_speed")->number_value();
        this->wheels->speed(forward * max_linear, turn * turn_factor * max_angular);
        this->commanding = nonzero;
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
