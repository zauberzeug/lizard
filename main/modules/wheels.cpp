#include "wheels.h"

const std::map<std::string, Variable_ptr> Wheels::get_defaults() {
    return {
        {"width", std::make_shared<NumberVariable>(1.0)},
        {"linear_speed", std::make_shared<NumberVariable>()},
        {"angular_speed", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
        {"locked", std::make_shared<BooleanVariable>(false)},
    };
}

// The gate properties that must stay in sync between a master and its shadows.
static constexpr const char *GATE_PROPERTIES[] = {"locked", "enabled"};

static bool is_gate_property(const std::string &property_name) {
    for (const char *gate : GATE_PROPERTIES) {
        if (property_name == gate) {
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
    this->properties.at("linear_speed")->number_value = (left_speed + right_speed) / 2;
    this->properties.at("angular_speed")->number_value = (right_speed - left_speed) / this->properties.at("width")->number_value;
}

bool Wheels::may_drive() const {
    return this->properties.at("enabled")->boolean_value && !this->properties.at("locked")->boolean_value;
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

    // Lock interlock: hold the wheels at standstill while enabled but locked, so the hold
    // engages even when no drive command arrives — e.g. the rule that set locked ran because
    // the host went silent. The hold is sent once on the rising edge of locked and refreshed
    // at a low rate to re-assert it after a motor reboot without flooding the bus.
    const bool should_hold = this->properties.at("enabled")->boolean_value && !this->may_drive();
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
        this->do_wheel_speeds(0.0, 0.0);
    }

    Module::step();
}

void Wheels::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "speed") {
        Module::expect(arguments, 2, numbery, numbery);
        if (this->may_drive()) {
            const double linear = arguments[0]->evaluate_number();
            const double angular = arguments[1]->evaluate_number();
            const double width = this->properties.at("width")->number_value;
            this->do_wheel_speeds(linear - angular * width / 2.0, linear + angular * width / 2.0);
        }
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
            this->sync_gate_properties(*this->shadow_modules.back());
        }
    } else {
        Module::call(method_name, arguments);
    }
}

void Wheels::sync_gate_properties(Module &shadow) const {
    for (const char *gate : GATE_PROPERTIES) {
        shadow.get_property(gate)->boolean_value = this->properties.at(gate)->boolean_value;
    }
}

void Wheels::write_property(const std::string property_name, const ConstExpression_ptr expression,
                            const bool from_expander) {
    Module::write_property(property_name, expression, from_expander);
    // Shadowed wheels mirror method calls but not property writes. Forward the gate properties
    // so a shadow stops together with its master. Write them one level deep via
    // Module::write_property (not the shadow's own override) to match the single-level
    // call_with_shadows and to avoid recursion on shadow chains or mutual shadows.
    if (is_gate_property(property_name)) {
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
    this->holding = false; // the standstill hold died with the motors; re-send it if locking persists
    this->last_applied_enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}
