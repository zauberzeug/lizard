#include "roboclaw_scissor_lift.h"
#include <cstdlib>
#include <memory>
#include <stdexcept>

REGISTER_MODULE_DEFAULTS(RoboClawScissorLift)

const std::map<std::string, Variable_ptr> RoboClawScissorLift::get_defaults() {
    return {
        {"position", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
    };
}

RoboClawScissorLift::RoboClawScissorLift(const std::string name, const RoboClawMotor_ptr motor1, const RoboClawMotor_ptr motor2)
    : Module(roboclaw_scissor_lift, name), motor1(motor1), motor2(motor2) {
    this->properties = RoboClawScissorLift::get_defaults();
}

static double motor_percent(const RoboClawMotor_ptr &motor) {
    if (!motor->is_calibrated()) {
        return 0.0;
    }
    const int32_t low = motor->get_limit_low();
    const int32_t high = motor->get_limit_high();
    const int32_t span = high - low;
    if (span == 0) {
        return 0.0;
    }
    return (static_cast<double>(motor->get_position()) - low) * 100.0 / span;
}

void RoboClawScissorLift::step() {
    if (this->motor1->is_calibrated() && this->motor2->is_calibrated()) {
        const double p1 = motor_percent(this->motor1);
        const double p2 = motor_percent(this->motor2);
        this->properties.at("position")->number_value = (p1 + p2) / 2.0;
    }

    if (this->properties.at("enabled")->boolean_value != this->enabled) {
        if (this->properties.at("enabled")->boolean_value) {
            this->enable();
        } else {
            this->disable();
        }
    }

    Module::step();
}

void RoboClawScissorLift::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "position") {
        if (arguments.size() < 1 || arguments.size() > 4) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery, numbery, numbery);
        const double percent = arguments[0]->evaluate_number();
        const uint32_t speed = arguments.size() > 1
                                   ? static_cast<uint32_t>(std::abs((int32_t)arguments[1]->evaluate_number()))
                                   : DEFAULT_SPEED;
        const uint32_t accel = arguments.size() > 2
                                   ? static_cast<uint32_t>(std::abs((int32_t)arguments[2]->evaluate_number()))
                                   : DEFAULT_ACCEL;
        const uint32_t deccel = arguments.size() > 3
                                    ? static_cast<uint32_t>(std::abs((int32_t)arguments[3]->evaluate_number()))
                                    : DEFAULT_DECCEL;
        this->drive(percent, speed, accel, deccel);
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->motor1->stop();
        this->motor2->stop();
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

void RoboClawScissorLift::drive(double percent, uint32_t speed, uint32_t accel, uint32_t deccel) {
    if (!this->enabled) {
        return;
    }
    if (!this->motor1->is_calibrated() || !this->motor2->is_calibrated()) {
        throw std::runtime_error("both motors must be calibrated with set_limits before driving the scissor lift");
    }

    auto target_ticks = [percent](const RoboClawMotor_ptr &motor) {
        const int32_t low = motor->get_limit_low();
        const int32_t high = motor->get_limit_high();
        const double t = low + (percent / 100.0) * (high - low);
        return static_cast<int32_t>(t);
    };

    this->motor1->position(target_ticks(this->motor1), speed, accel, deccel);
    this->motor2->position(target_ticks(this->motor2), speed, accel, deccel);
}

void RoboClawScissorLift::enable() {
    this->enabled = true;
    this->properties.at("enabled")->boolean_value = true;
    this->motor1->enable();
    this->motor2->enable();
}

void RoboClawScissorLift::disable() {
    this->motor1->disable();
    this->motor2->disable();
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}
