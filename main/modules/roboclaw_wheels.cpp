#include "roboclaw_wheels.h"
#include "../utils/timing.h"
#include <memory>

REGISTER_MODULE_DEFAULTS(RoboClawWheels)

const std::map<std::string, Variable_ptr> RoboClawWheels::get_defaults() {
    return {
        {"width", std::make_shared<NumberVariable>(1)},
        {"linear_speed", std::make_shared<NumberVariable>()},
        {"angular_speed", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
        {"m_per_tick", std::make_shared<NumberVariable>(1)},
    };
}

RoboClawWheels::RoboClawWheels(const std::string name, const RoboClawMotor_ptr left_motor, const RoboClawMotor_ptr right_motor)
    : Module(roboclaw_wheels, name), left_motor(left_motor), right_motor(right_motor) {
    this->properties = RoboClawWheels::get_defaults();
}

/* Catch unsigned wrap-around by detecting large jumps in encoder deltas */
static double difference_wrapped_u32(double current_value, double last_value) {
    double diff = current_value - last_value;
    if (diff > double(UINT32_MAX / 2))
        diff -= double(UINT32_MAX) + 1; // Underflow
    if (diff < -double(UINT32_MAX / 2))
        diff += double(UINT32_MAX) + 1; // Overflow
    return diff;
}

void RoboClawWheels::step() {
    double left_position = this->left_motor->get_position();
    double right_position = this->right_motor->get_position();

    if (initialized) {
        double d_left_position = difference_wrapped_u32(left_position, last_left_position);
        double d_right_position = difference_wrapped_u32(right_position, last_right_position);

        unsigned long int d_micros = micros_since(last_micros);
        const double m_per_tick = this->properties.at("m_per_tick")->number_value;
        double left_speed = (d_left_position * m_per_tick) / d_micros * 1000000;
        double right_speed = (d_right_position * m_per_tick) / d_micros * 1000000;
        this->properties.at("linear_speed")->number_value = (left_speed + right_speed) / 2;
        this->properties.at("angular_speed")->number_value = (right_speed - left_speed) / this->properties.at("width")->number_value;
    }

    last_micros = micros();
    last_left_position = left_position;
    last_right_position = right_position;
    initialized = true;

    // Check if the enabled property has changed
    if (this->properties.at("enabled")->boolean_value != this->enabled) {
        if (this->properties.at("enabled")->boolean_value) {
            this->enable();
        } else {
            this->disable();
        }
    }

    Module::step();
}

void RoboClawWheels::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
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
            const double half_width = this->properties.at("width")->number_value / 2.0;
            const double m_per_tick = this->properties.at("m_per_tick")->number_value;
            this->left_motor->speed((linear - angular * half_width) / m_per_tick);
            this->right_motor->speed((linear + angular * half_width) / m_per_tick);
        }
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->left_motor->power(0);
        this->right_motor->power(0);
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

void RoboClawWheels::enable() {
    this->enabled = true;
    this->properties.at("enabled")->boolean_value = true;
    this->left_motor->enable();
    this->right_motor->enable();
}

void RoboClawWheels::disable() {
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;
    this->left_motor->disable();
    this->right_motor->disable();
}
