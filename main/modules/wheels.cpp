#include "wheels.h"

std::map<std::string, Variable_ptr> Wheels::get_wheels_defaults() {
    return {
        {"width", std::make_shared<NumberVariable>(1.0)},
        {"linear_speed", std::make_shared<NumberVariable>()},
        {"angular_speed", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
        {"drivable", std::make_shared<BooleanVariable>(true)},
    };
}

Wheels::Wheels(const std::string name)
    : Module(name) {
    // Seed the shared properties the base unconditionally reads, so a subclass that forgets to
    // assign its own defaults still boots (a missing key would throw std::out_of_range, which the
    // main loop does not catch). Subclasses overwrite this with their own defaults in their ctor.
    this->properties = Wheels::get_wheels_defaults();
}

void Wheels::update_speeds(double left_speed, double right_speed) {
    this->properties.at("linear_speed")->number_value = (left_speed + right_speed) / 2;
    this->properties.at("angular_speed")->number_value = (right_speed - left_speed) / this->properties.at("width")->number_value;
}

bool Wheels::is_drivable() const {
    return this->properties.at("drivable")->boolean_value;
}

bool Wheels::gate_or_brake() {
    if (!this->properties.at("enabled")->boolean_value) {
        return false;
    }
    if (!this->is_drivable()) {
        // Handbrake: brake to a hold instead of ignoring, so the continuous command stream
        // stops the robot rather than holding the last commanded value.
        this->do_wheel_speeds(0.0, 0.0);
        return false;
    }
    return true;
}

void Wheels::step() {
    this->update_odometry();

    if (this->properties.at("enabled")->boolean_value != this->enabled) {
        if (this->properties.at("enabled")->boolean_value) {
            this->enable();
        } else {
            this->disable();
        }
    }

    Module::step();
}

void Wheels::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "speed") {
        Module::expect(arguments, 2, numbery, numbery);
        if (!this->gate_or_brake()) {
            return;
        }
        const double linear = arguments[0]->evaluate_number();
        const double angular = arguments[1]->evaluate_number();
        const double width = this->properties.at("width")->number_value;
        this->do_wheel_speeds(linear - angular * width / 2.0, linear + angular * width / 2.0);
    } else if (method_name == "enable") {
        Module::expect(arguments, 0);
        this->enable();
    } else if (method_name == "disable") {
        Module::expect(arguments, 0);
        this->disable();
    } else {
        Module::call(method_name, arguments);
    }
}

void Wheels::write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) {
    Module::write_property(property_name, expression, from_expander);
    // Shadowed wheels mirror method calls but not property writes. Forward the two gate
    // properties so a shadow stops together with its master. Write them one level deep via
    // Module::write_property (not the shadow's own override) to match the single-level
    // call_with_shadows and to avoid recursion on shadow chains or mutual shadows.
    if (property_name == "drivable" || property_name == "enabled") {
        for (auto const &module : this->shadow_modules) {
            module->Module::write_property(property_name, expression, from_expander);
        }
    }
}

void Wheels::enable() {
    this->enabled = true;
    this->properties.at("enabled")->boolean_value = true;
    this->do_enable();
}

void Wheels::disable() {
    this->do_disable();
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}
