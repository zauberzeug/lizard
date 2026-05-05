#include "dual_drive_wheels.h"
#include <memory>

REGISTER_MODULE_DEFAULTS(DualDriveWheels)

const std::map<std::string, Variable_ptr> DualDriveWheels::get_defaults() {
    return {
        {"width", std::make_shared<NumberVariable>(1.0)},
        {"linear_speed", std::make_shared<NumberVariable>()},
        {"angular_speed", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
    };
}

DualDriveWheels::DualDriveWheels(const std::string name, const DualDriveMotor_ptr left_motor, const DualDriveMotor_ptr right_motor)
    : Module(dual_drive_wheels, name), left_motor(left_motor), right_motor(right_motor) {
    this->properties = DualDriveWheels::get_defaults();
}

bool DualDriveWheels::is_enabled() const {
    return this->properties.at("enabled")->boolean_value;
}

void DualDriveWheels::step() {
    const double left_speed = this->left_motor->get_speed();
    const double right_speed = this->right_motor->get_speed();
    const double width = this->properties.at("width")->number_value;
    this->properties.at("linear_speed")->number_value = (left_speed + right_speed) / 2;
    this->properties.at("angular_speed")->number_value = (right_speed - left_speed) / width;

    bool desired = this->is_enabled();
    if (desired != this->last_applied_enabled) {
        if (desired) {
            this->enable();
        } else {
            this->disable();
        }
    }

    Module::step();
}

void DualDriveWheels::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "speed") {
        Module::expect(arguments, 2, numbery, numbery);
        if (this->is_enabled()) {
            const double linear = arguments[0]->evaluate_number();
            const double angular = arguments[1]->evaluate_number();
            const double width = this->properties.at("width")->number_value;
            this->left_motor->speed(linear - angular * width / 2.0, 0);
            this->right_motor->speed(linear + angular * width / 2.0, 0);
        }
    } else if (method_name == "drive") {
        Module::expect(arguments, 2, numbery, numbery);
        if (this->is_enabled()) {
            const float linear_speed = arguments[0]->evaluate_number();
            const float distance = arguments[1]->evaluate_number();
            this->left_motor->drive_meters(linear_speed, distance);
            this->right_motor->drive_meters(linear_speed, distance);
        }
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->left_motor->disable();
        this->right_motor->disable();
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->left_motor->stop();
        this->right_motor->stop();
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

void DualDriveWheels::enable() {
    this->properties.at("enabled")->boolean_value = true;
    this->last_applied_enabled = true;
    this->left_motor->enable();
    this->right_motor->enable();
}

void DualDriveWheels::disable() {
    this->left_motor->disable();
    this->right_motor->disable();
    this->properties.at("enabled")->boolean_value = false;
    this->last_applied_enabled = false;
}
