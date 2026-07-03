#include "roboclaw_wheels.h"
#include "../utils/timing.h"
#include "module_helpers.h"
#include "roboclaw_motor.h"
#include <memory>

static Module_ptr create_roboclaw_wheels(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    Module::expect(arguments, 2, identifier, identifier);
    const RoboClawMotor_ptr left_motor = get_module_argument<RoboClawMotor>(arguments[0]);
    const RoboClawMotor_ptr right_motor = get_module_argument<RoboClawMotor>(arguments[1]);
    return std::make_shared<RoboClawWheels>(name, left_motor, right_motor);
}
REGISTER_MODULE(RoboClawWheels, &create_roboclaw_wheels)

const std::map<std::string, Variable_ptr> RoboClawWheels::get_defaults() {
    auto defaults = Wheels::get_defaults();
    defaults["m_per_tick"] = std::make_shared<NumberVariable>(1);
    return defaults;
}

RoboClawWheels::RoboClawWheels(const std::string name, const RoboClawMotor_ptr left_motor, const RoboClawMotor_ptr right_motor)
    : Wheels(name), left_motor(left_motor), right_motor(right_motor) {
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

void RoboClawWheels::update_odometry() {
    double left_position = this->left_motor->get_position();
    double right_position = this->right_motor->get_position();

    if (this->initialized) {
        double d_left_position = difference_wrapped_u32(left_position, this->last_left_position);
        double d_right_position = difference_wrapped_u32(right_position, this->last_right_position);

        unsigned long int d_micros = micros_since(this->last_micros);
        const double m_per_tick = this->properties.at("m_per_tick")->number_value;
        double left_speed = (d_left_position * m_per_tick) / d_micros * 1000000;
        double right_speed = (d_right_position * m_per_tick) / d_micros * 1000000;
        this->update_speeds(left_speed, right_speed);
    }

    this->last_micros = micros();
    this->last_left_position = left_position;
    this->last_right_position = right_position;
    this->initialized = true;
}

void RoboClawWheels::do_wheel_speeds(double left, double right) {
    const double m_per_tick = this->properties.at("m_per_tick")->number_value;
    this->left_motor->speed(left / m_per_tick);
    this->right_motor->speed(right / m_per_tick);
}

void RoboClawWheels::do_enable() {
    this->left_motor->enable();
    this->right_motor->enable();
}

void RoboClawWheels::do_disable() {
    this->left_motor->disable();
    this->right_motor->disable();
}

void RoboClawWheels::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "power") {
        Module::expect(arguments, 2, numbery, numbery);
        if (!this->gate_or_brake()) {
            return;
        }
        this->left_motor->power(arguments[0]->evaluate_number());
        this->right_motor->power(arguments[1]->evaluate_number());
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->left_motor->power(0);
        this->right_motor->power(0);
    } else {
        Wheels::call(method_name, arguments);
    }
}
