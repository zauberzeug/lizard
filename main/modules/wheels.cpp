#include "wheels.h"
#include "../compilation/expressions.h"
#include <algorithm>
#include <cmath>
#include "../utils/timing.h"
#include "../utils/uart.h"

const std::map<std::string, Variable_ptr> Wheels::get_defaults() {
    return {
        {"width", std::make_shared<NumberVariable>(1.0)},
        {"max_wheel_speed", std::make_shared<NumberVariable>(0.0)},
        {"linear_speed", std::make_shared<NumberVariable>()},
        {"angular_speed", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
        {"locked", std::make_shared<BooleanVariable>(false)},
        {"drive_command_age", std::make_shared<IntegerVariable>(0)},
        {"drive_command_timeout", std::make_shared<NumberVariable>(1.0)},
    };
}

// The properties that must stay in sync between a master and its shadows: the gates and the
// dead man's timeout, so shadowed wheels stop together with (and as early as) their master,
// and the speed limit, so a shadow does not outrun a capped master and fight it.
static constexpr const char *SHARED_PROPERTIES[] = {"locked", "enabled", "drive_command_timeout", "max_wheel_speed"};

static bool is_shared_property(const std::string &property_name) {
    for (const char *shared : SHARED_PROPERTIES) {
        if (property_name == shared) {
            return true;
        }
    }
    return false;
}

Wheels::Wheels(const std::string name, const std::map<std::string, Variable_ptr> &defaults)
    : Module(name) {
    this->properties = defaults;
}

void Wheels::update_speeds(double left_speed, double right_speed) {
    this->properties.at("linear_speed")->set_number_value((left_speed + right_speed) / 2);
    this->properties.at("angular_speed")->set_number_value((right_speed - left_speed) / this->properties.at("width")->number_value());
}

bool Wheels::may_drive() const {
    return this->properties.at("enabled")->boolean_value() && !this->properties.at("locked")->boolean_value();
}

void Wheels::step() {
    this->update_odometry();

    this->sync_enabled();

    const unsigned long drive_command_age = millis_since(this->last_drive_command_millis);
    this->properties.at("drive_command_age")->set_integer_value(drive_command_age);

    // Dead man's switch: a non-zero drive command that is not refreshed in time trips the switch,
    // whether the sender lost its connection or simply stopped sending. A tripped switch holds the
    // wheels at standstill below, so a stop that does not reach the motors (a failing motor send,
    // a dropped CAN frame, a motor reboot) is re-asserted; the next drive command releases it.
    const double timeout = this->properties.at("drive_command_timeout")->number_value();
    if (this->moving && timeout > 0.0 && drive_command_age > timeout * 1000.0) {
        this->moving = false;
        this->stopped = true;
        echo("warning: wheels %s stopped: no drive command for %lu ms", this->name.c_str(), drive_command_age);
    }

    // Standstill hold: hold the wheels while enabled but locked or stopped by the dead man's
    // switch, so the hold engages even when no drive command arrives — e.g. the rule that set
    // locked ran because the host went silent. The hold is sent once on its rising edge and
    // refreshed at a low rate to re-assert it after a lost send or a motor reboot without
    // flooding the bus.
    const bool should_hold = this->properties.at("enabled")->boolean_value() && (!this->may_drive() || this->stopped);
    if (!should_hold) {
        this->holding = false;
    } else if (this->holding) {
        if (++this->hold_cycle >= HOLD_REFRESH_CYCLES) {
            this->hold_cycle = 0;
            this->do_wheel_speeds(0.0, 0.0);
        }
    } else {
        this->holding = true;
        this->hold_cycle = 0;
        this->moving = false; // the hold is a stop; the dead man's switch has nothing left to do
        this->do_wheel_speeds(0.0, 0.0);
    }

    // Stop repeat: a single zero-speed frame after motion can be lost on the bus or refused by a drive (seen
    // on some tracks, which then crept on at the last setpoint), so it is re-sent at an interval for a while —
    // often enough to catch a lost frame, rarely enough not to flood the bus. A hold takes over the standstill
    // on its own, and any other `speed()`, `power()` or `off()` that went through ends the repeat early.
    if (this->repeating_stop) {
        if (this->holding || !this->may_drive() || millis_since(this->stop_repeat_millis) >= STOP_REPEAT_MILLIS) {
            this->repeating_stop = false;
        } else if (millis_since(this->stop_repeat_sent_millis) >= STOP_REPEAT_INTERVAL_MILLIS) {
            this->stop_repeat_sent_millis = millis();
            this->do_wheel_speeds(0.0, 0.0);
        }
    }

    Module::step();
}

void Wheels::note_drive_command(bool applied, bool nonzero) {
    this->last_drive_command_millis = millis();
    if (applied && nonzero) {
        this->moving = true;
    }
}

void Wheels::note_drive_command_sent(bool nonzero) {
    this->stopped = false;
    this->moving = nonzero;
}

void Wheels::do_wheel_powers(double left, double right) {
    throw std::runtime_error("module \"" + this->name + "\" has no torque control");
}

void Wheels::do_off() {
    throw std::runtime_error("module \"" + this->name + "\" has no idle state");
}

void Wheels::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "speed") {
        Module::expect(arguments, 2, numbery, numbery);
        const double linear = arguments[0]->evaluate_number();
        const double angular = arguments[1]->evaluate_number();
        if (!std::isfinite(linear) || !std::isfinite(angular)) {
            throw std::runtime_error("module \"" + this->name + "\" expects finite speeds");
        }
        const bool nonzero = linear != 0.0 || angular != 0.0;
        const bool applied = this->may_drive();
        // A stop is due after motion, after a tripped dead man's switch (its release must not be a single, losable
        // zero) and while a stop is already repeating (a further stop restarts the repeat).
        const bool was_stopping = this->moving || this->stopped || this->repeating_stop;
        this->note_drive_command(applied, nonzero);
        if (applied) {
            const double width = this->properties.at("width")->number_value();
            double left = linear - angular * width / 2.0;
            double right = linear + angular * width / 2.0;
            // Saturation: scale both wheels down together, so the robot keeps its direction instead of veering
            // when the drive cuts off only the faster wheel.
            const double max_wheel_speed = this->properties.at("max_wheel_speed")->number_value();
            const double fastest_wheel = std::max(std::abs(left), std::abs(right));
            if (max_wheel_speed > 0.0 && fastest_wheel > max_wheel_speed) {
                left *= max_wheel_speed / fastest_wheel;
                right *= max_wheel_speed / fastest_wheel;
            }
            // Arm the repeat before the send, so a stop that fails to go out is retried as well.
            if (!nonzero && was_stopping) {
                this->repeating_stop = true;
                this->stop_repeat_millis = millis();
                this->stop_repeat_sent_millis = this->stop_repeat_millis;
            }
            this->do_wheel_speeds(left, right);
            if (nonzero) {
                this->repeating_stop = false; // only once the motion went out, so a failed send keeps the stop repeating
            }
            this->note_drive_command_sent(nonzero);
        }
    } else if (method_name == "power") {
        Module::expect(arguments, 2, numbery, numbery);
        const double left = arguments[0]->evaluate_number();
        const double right = arguments[1]->evaluate_number();
        if (!std::isfinite(left) || !std::isfinite(right)) {
            throw std::runtime_error("module \"" + this->name + "\" expects finite powers");
        }
        const bool nonzero = left != 0.0 || right != 0.0;
        const bool applied = this->may_drive();
        this->note_drive_command(applied, nonzero);
        if (applied) {
            this->do_wheel_powers(left, right);
            this->repeating_stop = false; // only once the command went out, so a failed one keeps the stop repeating
            this->note_drive_command_sent(nonzero);
        }
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->do_off();
        this->repeating_stop = false;         // only once the motors idle, so a failed off() keeps the stop repeating
        this->note_drive_command_sent(false); // idle motors are a stop; the dead man's switch must not re-activate them
    } else if (method_name == "enable") {
        Module::expect(arguments, 0);
        this->enable();
    } else if (method_name == "disable") {
        Module::expect(arguments, 0);
        this->disable();
    } else if (method_name == "shadow") {
        const size_t shadow_count = this->shadow_modules.size();
        Module::call(method_name, arguments);
        // Property writes only forward to already-attached shadows, so a freshly attached shadow
        // could keep driving with stale gate values while the master holds. Sync it once on attach.
        // (Module::call skips the attach for self-shadows, hence the size check.)
        if (this->shadow_modules.size() > shadow_count) {
            this->sync_shared_properties(*this->shadow_modules.back());
        }
    } else {
        Module::call(method_name, arguments);
    }
}

void Wheels::sync_shared_properties(Module &shadow) const {
    for (const char *shared : SHARED_PROPERTIES) {
        const Variable_ptr source = this->properties.at(shared);
        const Variable_ptr target = shadow.get_property(shared);
        switch (source->type) {
        case boolean:
            target->set_boolean_value(source->boolean_value());
            break;
        case number:
            target->set_number_value(source->number_value());
            break;
        default:
            throw std::runtime_error("unexpected type of shared wheels property \"" + std::string(shared) + "\"");
        }
    }
}

void Wheels::write_property(const std::string property_name, ConstExpression_ptr expression,
                            const bool from_expander) {
    if (property_name == "max_wheel_speed") {
        // Evaluate once and store the checked value, so validation and assignment cannot disagree.
        const double max_wheel_speed = expression->evaluate_number();
        if (!std::isfinite(max_wheel_speed) || max_wheel_speed < 0.0) {
            throw std::runtime_error("max_wheel_speed of module \"" + this->name + "\" must be finite and >= 0");
        }
        expression = std::make_shared<NumberExpression>(max_wheel_speed);
    }
    Module::write_property(property_name, expression, from_expander);
    // Shadowed wheels mirror method calls but not property writes. Forward the shared properties
    // so a shadow stops together with its master. Write them one level deep via
    // Module::write_property (not the shadow's own override) to match the single-level
    // call_with_shadows and to avoid recursion on shadow chains or mutual shadows.
    if (is_shared_property(property_name)) {
        for (auto const &module : this->shadow_modules) {
            module->Module::write_property(property_name, expression, from_expander);
        }
    }
}

void Wheels::disable() {
    // The standstill hold dies with the motors; clear it first so a throwing do_disable()
    // cannot leave it stale, and re-send it if locking persists.
    this->holding = false;
    this->repeating_stop = false;
    Module::disable();
    // Only a completed disable is a stop; while it stays pending, the dead man's switch may still fire.
    this->moving = false;
    this->stopped = false;
}
