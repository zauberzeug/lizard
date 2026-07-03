#include "dunker_wheels.h"
#include "dunker_motor.h"
#include "module_helpers.h"
#include <memory>

static Module_ptr create_dunker_wheels(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    Module::expect(arguments, 2, identifier, identifier);
    const DunkerMotor_ptr left_motor = get_module_argument<DunkerMotor>(arguments[0]);
    const DunkerMotor_ptr right_motor = get_module_argument<DunkerMotor>(arguments[1]);
    return std::make_shared<DunkerWheels>(name, left_motor, right_motor);
}
REGISTER_MODULE(DunkerWheels, &create_dunker_wheels)

DunkerWheels::DunkerWheels(const std::string name, const DunkerMotor_ptr left_motor, const DunkerMotor_ptr right_motor)
    : Wheels(name), left_motor(left_motor), right_motor(right_motor) {
}

void DunkerWheels::update_odometry() {
    const double left_speed = this->left_motor->get_speed();
    const double right_speed = this->right_motor->get_speed();
    this->update_speeds(left_speed, right_speed);
}

void DunkerWheels::do_wheel_speeds(double left, double right) {
    this->left_motor->speed(left);
    this->right_motor->speed(right);
}

void DunkerWheels::do_enable() {
    this->left_motor->enable();
    this->right_motor->enable();
}

void DunkerWheels::do_disable() {
    this->left_motor->disable();
    this->right_motor->disable();
}
