#include "odrive_wheels.h"
#include "../utils/timing.h"

ODriveWheels::ODriveWheels(const std::string name, ODriveMotor *const left_motor, ODriveMotor *const right_motor)
    : Module(odrive_wheels, name), left_motor(left_motor), right_motor(right_motor) {
    this->properties["width"] = new NumberVariable(1);
    this->properties["linear_speed"] = new NumberVariable();
    this->properties["angular_speed"] = new NumberVariable();
}

void ODriveWheels::step() {
    static unsigned long int last_micros;
    static double last_left_position;
    static double last_right_position;
    static bool initialized = false;

    double left_position = this->left_motor->get_position();
    double right_position = this->right_motor->get_position();

    if (initialized) {
        unsigned long int d_micros = micros_since(last_micros);
        double left_speed = (left_position - last_left_position) / d_micros * 1000000;
        double right_speed = (right_position - last_right_position) / d_micros * 1000000;
        this->properties.at("linear_speed")->number_value = (left_speed + right_speed) / 2;
        this->properties.at("angular_speed")->number_value = (right_speed - left_speed) / this->properties.at("width")->number_value;
    }

    last_micros = micros();
    last_left_position = left_position;
    last_right_position = right_position;
    initialized = true;

    Module::step();
}

void ODriveWheels::call(const std::string method_name, const std::vector<Expression_ptr> arguments) {
    if (method_name == "power") {
        Module::expect(arguments, 2, numbery, numbery);
        this->left_motor->power(arguments[0]->evaluate_number());
        this->right_motor->power(arguments[1]->evaluate_number());
    } else if (method_name == "speed") {
        Module::expect(arguments, 2, numbery, numbery);
        double linear = arguments[0]->evaluate_number();
        double angular = arguments[1]->evaluate_number();
        double width = this->properties.at("width")->number_value;
        this->left_motor->speed(linear - angular * width / 2.0);
        this->right_motor->speed(linear + angular * width / 2.0);
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->left_motor->off();
        this->right_motor->off();
    } else {
        Module::call(method_name, arguments);
    }
}
