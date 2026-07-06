#include "wheels.h"

const std::map<std::string, Variable_ptr> Wheels::get_defaults() {
    return {
        {"width", std::make_shared<NumberVariable>(1.0)},
        {"linear_speed", std::make_shared<NumberVariable>()},
        {"angular_speed", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
        {"drivable", std::make_shared<BooleanVariable>(true)},
    };
}

Wheels::Wheels(const std::string name, const std::map<std::string, Variable_ptr> &defaults)
    : Module(name) {
    this->properties = defaults;
}

void Wheels::update_speeds(double left_speed, double right_speed) {
    this->properties.at("linear_speed")->number_value = (left_speed + right_speed) / 2;
    this->properties.at("angular_speed")->number_value = (right_speed - left_speed) / this->properties.at("width")->number_value;
}

bool Wheels::may_drive() const {
    return this->properties.at("enabled")->boolean_value && this->properties.at("drivable")->boolean_value;
}

void Wheels::step() {
    this->update_odometry();

    if (this->properties.at("enabled")->boolean_value != this->last_applied_enabled) {
        if (this->properties.at("enabled")->boolean_value) {
            this->enable();
        } else {
            this->disable();
        }
    }

    if (this->properties.at("enabled")->boolean_value && !this->properties.at("drivable")->boolean_value) {
        // Handbrake: keep braking to a hold every cycle, so it engages even when no drive command
        // arrives — e.g. the rule that cleared drivable ran because the host went silent.
        this->do_wheel_speeds(0.0, 0.0);
    }

    Module::step();
}

void Wheels::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "speed") {
        Module::expect(arguments, 2, numbery, numbery);
        if (!this->may_drive()) {
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
    } else if (method_name == "shadow") {
        Module::call(method_name, arguments);
        // Property writes only forward to already-attached shadows, so a shadow attached after
        // drivable/enabled were changed would start out with stale defaults and keep driving while
        // the master holds. Sync the two gate properties onto the freshly attached shadow.
        if (!this->shadow_modules.empty()) {
            const auto &shadow = this->shadow_modules.back();
            shadow->get_property("drivable")->boolean_value = this->properties.at("drivable")->boolean_value;
            shadow->get_property("enabled")->boolean_value = this->properties.at("enabled")->boolean_value;
        }
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
    this->last_applied_enabled = true;
    this->properties.at("enabled")->boolean_value = true;
    this->do_enable();
}

void Wheels::disable() {
    this->do_disable();
    this->last_applied_enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}
