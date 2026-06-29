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
}

bool Wheels::can_drive() const {
    return this->enabled && this->properties.at("drivable")->boolean_value;
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
        if (!this->can_drive()) {
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
    // Shadowed wheels (e.g. front tracks) mirror method calls, but not property writes. Forward the
    // drivable gate so shadows stop driving together with their master.
    if (property_name == "drivable") {
        for (auto const &module : this->shadow_modules) {
            module->write_property(property_name, expression, from_expander);
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
