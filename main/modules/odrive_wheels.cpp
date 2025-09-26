#include "odrive_wheels.h"
#include "../utils/timing.h"
#include <cmath>
#include <memory>

REGISTER_MODULE_DEFAULTS(ODriveWheels)

const std::map<std::string, Variable_ptr> ODriveWheels::get_defaults() {
    return {
        {"width", std::make_shared<NumberVariable>(1.0)},
        {"linear_speed", std::make_shared<NumberVariable>()},
        {"angular_speed", std::make_shared<NumberVariable>()},
        {"test_linear_speed", std::make_shared<NumberVariable>()},
        {"test_angular_speed", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
    };
}

ODriveWheels::ODriveWheels(const std::string name, const ODriveMotor_ptr left_motor, const ODriveMotor_ptr right_motor)
    : Module(odrive_wheels, name), left_motor(left_motor), right_motor(right_motor) {
    this->properties = ODriveWheels::get_defaults();
}

void ODriveWheels::step() {
    double left_position = this->left_motor->get_position();
    double right_position = this->right_motor->get_position();

    if (this->initialized) {
        unsigned long int d_micros = micros_since(this->last_micros);
        if (d_micros >= 2000) {
            double left_speed = (left_position - this->last_left_position) / d_micros * 1000000;
            double right_speed = (right_position - this->last_right_position) / d_micros * 1000000;
            const double max_speed_mps = 10.0;
            if (std::isfinite(left_speed) && std::isfinite(right_speed) &&
                std::fabs(left_speed) < max_speed_mps && std::fabs(right_speed) < max_speed_mps) {
                this->properties.at("linear_speed")->number_value = (left_speed + right_speed) / 2;
                this->properties.at("angular_speed")->number_value = (right_speed - left_speed) / this->properties.at("width")->number_value;
            }
        }
    }

    // Compute speeds directly from motor-reported speeds for comparison, this is for testing. Remove this for production.
    const double left_measured_speed = this->left_motor->get_speed();
    const double right_measured_speed = this->right_motor->get_speed();
    const double width = this->properties.at("width")->number_value;
    this->properties.at("test_linear_speed")->number_value = (left_measured_speed + right_measured_speed) / 2.0;
    this->properties.at("test_angular_speed")->number_value = (right_measured_speed - left_measured_speed) / width;

    this->last_micros = micros();
    this->last_left_position = left_position;
    this->last_right_position = right_position;
    this->initialized = true;

    if (this->properties.at("enabled")->boolean_value != this->enabled) {
        if (this->properties.at("enabled")->boolean_value) {
            this->enable();
        } else {
            this->disable();
        }
    }

    Module::step();
}

void ODriveWheels::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "power") {
        Module::expect(arguments, 2, numbery, numbery);
        if (this->properties.at("enabled")->boolean_value) {
            this->left_motor->power(arguments[0]->evaluate_number());
            this->right_motor->power(arguments[1]->evaluate_number());
        }
    } else if (method_name == "speed") {
        Module::expect(arguments, 2, numbery, numbery);
        if (this->properties.at("enabled")->boolean_value) {
            double linear = arguments[0]->evaluate_number();
            double angular = arguments[1]->evaluate_number();
            double width = this->properties.at("width")->number_value;
            this->left_motor->speed(linear - angular * width / 2.0);
            this->right_motor->speed(linear + angular * width / 2.0);
        }
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->left_motor->off();
        this->right_motor->off();
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

void ODriveWheels::enable() {
    this->enabled = true;
    this->properties.at("enabled")->boolean_value = true;
    this->left_motor->enable();
    this->right_motor->enable();
}

void ODriveWheels::disable() {
    this->left_motor->disable();
    this->right_motor->disable();
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}
