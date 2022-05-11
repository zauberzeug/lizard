#include "roboclaw_wheels.h"
#include "../utils/timing.h"
#include <memory>

RoboClawWheels::RoboClawWheels(const std::string name, const RoboClawMotor_ptr left_motor, const RoboClawMotor_ptr right_motor)
    : Module(roboclaw_wheels, name), left_motor(left_motor), right_motor(right_motor) {
    this->properties["width"] = std::make_shared<NumberVariable>(1);
    this->properties["linear_speed"] = std::make_shared<NumberVariable>();
    this->properties["angular_speed"] = std::make_shared<NumberVariable>();
    this->properties["enabled"] = std::make_shared<BooleanVariable>(true);
    this->properties["m_per_tick"] = std::make_shared<NumberVariable>(1);
}

void RoboClawWheels::step() {
    double left_position = this->left_motor->get_position();
    double right_position = this->right_motor->get_position();

    if (initialized) {
        double d_left_position = left_position - last_left_position;
        double d_right_position = right_position - last_right_position;

        /* Catch unsigned wrap-around by detecting large jumps in encoder deltas */
        if (std::abs(d_left_position) < (UINT32_MAX / 2) && std::abs(d_right_position) < (UINT32_MAX / 2)) {
            unsigned long int d_micros = micros_since(last_micros);
            const double scale = this->properties.at("m_per_tick")->number_value;
            double left_speed = (d_left_position * scale) / d_micros * 1000000;
            double right_speed = (d_right_position * scale) / d_micros * 1000000;
            this->properties.at("linear_speed")->number_value = (left_speed + right_speed) / 2;
            this->properties.at("angular_speed")->number_value = (right_speed - left_speed) / this->properties.at("width")->number_value;
        }
    }

    last_micros = micros();
    last_left_position = left_position;
    last_right_position = right_position;
    initialized = true;

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
            double width = this->properties.at("width")->number_value;
            this->left_motor->speed(linear - angular * width / 2.0);
            this->right_motor->speed(linear + angular * width / 2.0);
        }
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->left_motor->power(0);
        this->right_motor->power(0);
    } else {
        Module::call(method_name, arguments);
    }
}
