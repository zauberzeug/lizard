#include "rmd_axis.h"
#include "../utils/uart.h"
#include <stdexcept>

REGISTER_MODULE_DEFAULTS(RmdAxis)

const std::map<std::string, Variable_ptr> RmdAxis::get_defaults() {
    return {
        {"enabled", std::make_shared<BooleanVariable>(true)},
    };
}

RmdAxis::RmdAxis(const std::string name, const Rmd8xProV2_ptr motor, const Input_ptr top_endstop, const bool inverted)
    : Module(rmd_axis, name), motor(motor), top_endstop(top_endstop), inverted(inverted) {
    this->properties = RmdAxis::get_defaults();
}

bool RmdAxis::can_move_positive(const float value) const {
    if (!this->enabled) {
        return false;
    }
    bool endstop_active = this->top_endstop && this->top_endstop->get_property("active")->boolean_value;
    if (!endstop_active) {
        return true;
    }
    // If endstop is active, block motion towards it: up is positive when not inverted, negative when inverted
    if ((!this->inverted && value > 0) || (this->inverted && value < 0)) {
        echo("%s.can_move_positive: Endstop triggered", this->name.c_str());
        return false;
    }
    return true;
}

void RmdAxis::step() {
    if (this->properties.at("enabled")->boolean_value != this->enabled) {
        if (this->properties.at("enabled")->boolean_value) {
            this->enable();
        } else {
            this->disable();
        }
    }

    float speed = this->motor->get_speed();
    if (!this->can_move_positive(speed)) {
        this->motor->hold(); // Use hold to get new speed information, stop will not update the speed
    }
    Module::step();
}

void RmdAxis::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "position") {
        if (arguments.size() < 2 || arguments.size() > 3) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery, numbery);
        float distance = arguments[0]->evaluate_number() - this->motor->get_position();
        if (this->can_move_positive(distance)) {
            this->motor->position(arguments[0]->evaluate_number() * this->motor->get_ratio(), arguments[1]->evaluate_number());
        }
    } else if (method_name == "speed") {
        if (arguments.size() < 1 || arguments.size() > 2) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery);
        float speed = arguments[0]->evaluate_number();
        if (this->can_move_positive(speed)) {
            this->motor->speed(speed);
        } else {
            this->motor->stop();
        }
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->motor->stop();
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

void RmdAxis::enable() {
    this->properties.at("enabled")->boolean_value = true;
    this->enabled = true;
    this->motor->enable();
}

void RmdAxis::disable() {
    this->motor->stop();
    this->motor->disable();
    this->properties.at("enabled")->boolean_value = false;
    this->enabled = false;
}
