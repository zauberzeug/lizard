#include "odrive_wheels.h"
#include "../utils/timing.h"
#include "module_helpers.h"
#include "odrive_motor.h"
#include <memory>

static Module_ptr create_odrive_wheels(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    Module::expect(arguments, 2, identifier, identifier);
    const ODriveMotor_ptr left_motor = get_module_argument<ODriveMotor>(arguments[0]);
    const ODriveMotor_ptr right_motor = get_module_argument<ODriveMotor>(arguments[1]);
    return std::make_shared<ODriveWheels>(name, left_motor, right_motor);
}
REGISTER_MODULE(ODriveWheels, &create_odrive_wheels)

const std::map<std::string, Variable_ptr> ODriveWheels::get_defaults() {
    return Wheels::get_wheels_defaults();
}

ODriveWheels::ODriveWheels(const std::string name, const ODriveMotor_ptr left_motor, const ODriveMotor_ptr right_motor)
    : Wheels(name), left_motor(left_motor), right_motor(right_motor) {
    this->properties = ODriveWheels::get_defaults();
}

void ODriveWheels::update_odometry() {
    double left_position = this->left_motor->get_position();
    double right_position = this->right_motor->get_position();

    if (this->initialized) {
        unsigned long int d_micros = micros_since(this->last_micros);
        double left_speed = (left_position - this->last_left_position) / d_micros * 1000000;
        double right_speed = (right_position - this->last_right_position) / d_micros * 1000000;
        this->update_speeds(left_speed, right_speed);
    }

    this->last_micros = micros();
    this->last_left_position = left_position;
    this->last_right_position = right_position;
    this->initialized = true;
}

void ODriveWheels::do_wheel_speeds(double left, double right) {
    this->left_motor->speed(left);
    this->right_motor->speed(right);
}

void ODriveWheels::do_enable() {
    this->left_motor->enable();
    this->right_motor->enable();
}

void ODriveWheels::do_disable() {
    this->left_motor->disable();
    this->right_motor->disable();
}

void ODriveWheels::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "power") {
        Module::expect(arguments, 2, numbery, numbery);
        if (!this->gate_or_brake()) {
            return;
        }
        this->left_motor->power(arguments[0]->evaluate_number());
        this->right_motor->power(arguments[1]->evaluate_number());
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->left_motor->off();
        this->right_motor->off();
    } else {
        Wheels::call(method_name, arguments);
    }
}
