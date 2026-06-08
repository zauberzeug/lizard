#include "roboclaw_motor.h"
#include <cstdlib>
#include <memory>
#include <stdexcept>

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

REGISTER_MODULE_DEFAULTS(RoboClawMotor)

const std::map<std::string, Variable_ptr> RoboClawMotor::get_defaults() {
    return {
        {"position", std::make_shared<IntegerVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
        {"calibrated", std::make_shared<BooleanVariable>(false)},
        {"limit_low", std::make_shared<IntegerVariable>()},
        {"limit_high", std::make_shared<IntegerVariable>()},
        {"default_speed", std::make_shared<IntegerVariable>(40)},
        {"default_accel", std::make_shared<IntegerVariable>(80)},
        {"default_deccel", std::make_shared<IntegerVariable>(80)},
    };
}

RoboClawMotor::RoboClawMotor(const std::string name, const RoboClaw_ptr roboclaw, const unsigned int motor_number)
    : Module(roboclaw_motor, name), motor_number(constrain(motor_number, 1, 2)), roboclaw(roboclaw) {
    if (this->motor_number != motor_number) {
        throw std::runtime_error("illegal motor number");
    }
    this->properties = RoboClawMotor::get_defaults();
}

void RoboClawMotor::step() {
    if (this->properties.at("enabled")->boolean_value != this->enabled) {
        if (this->properties.at("enabled")->boolean_value) {
            this->enable();
        } else {
            this->disable();
        }
    }

    if (++this->step_count % 10 == 0) {
        uint8_t status;
        bool valid;
        int64_t position = this->motor_number == 1 ? this->roboclaw->ReadEncM1(&status, &valid) : this->roboclaw->ReadEncM2(&status, &valid);
        if (!valid) {
            throw std::runtime_error("could not read motor position");
        }
        this->properties["position"]->integer_value = position;
    }
    Module::step();
}

void RoboClawMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "power") {
        Module::expect(arguments, 1, numbery);
        this->power(arguments[0]->evaluate_number());
    } else if (method_name == "speed") {
        Module::expect(arguments, 1, numbery);
        this->speed(arguments[0]->evaluate_number());
    } else if (method_name == "zero") {
        bool success = this->motor_number == 1 ? this->roboclaw->SetEncM1(0) : this->roboclaw->SetEncM2(0);
        if (!success) {
            throw std::runtime_error("could not reset position");
        }
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->stop();
    } else if (method_name == "set_limits") {
        Module::expect(arguments, 2, integer, integer);
        this->set_limits(arguments[0]->evaluate_integer(), arguments[1]->evaluate_integer());
    } else if (method_name == "position") {
        if (arguments.size() < 1 || arguments.size() > 4) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, integer, numbery, numbery, numbery);
        int32_t target = arguments[0]->evaluate_integer();
        uint32_t speed = arguments.size() > 1
                             ? static_cast<uint32_t>(std::abs((int32_t)arguments[1]->evaluate_number()))
                             : static_cast<uint32_t>(this->properties.at("default_speed")->integer_value);
        uint32_t accel = arguments.size() > 2
                             ? static_cast<uint32_t>(std::abs((int32_t)arguments[2]->evaluate_number()))
                             : static_cast<uint32_t>(this->properties.at("default_accel")->integer_value);
        uint32_t deccel = arguments.size() > 3
                              ? static_cast<uint32_t>(std::abs((int32_t)arguments[3]->evaluate_number()))
                              : static_cast<uint32_t>(this->properties.at("default_deccel")->integer_value);
        this->position(target, speed, accel, deccel);
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

int64_t RoboClawMotor::get_position() const {
    return this->properties.at("position")->integer_value;
}

void RoboClawMotor::power(double value) {
    if (!this->enabled) {
        return;
    }
    unsigned short int duty = (short int)(constrain(value, -1, 1) * 32767);

    const int max_retries = 4;
    for (int retries = 0; retries < max_retries; ++retries) {
        bool success = this->motor_number == 1 ? this->roboclaw->DutyM1(duty) : this->roboclaw->DutyM2(duty);
        if (success) {
            return;
        }
    }
    throw std::runtime_error("could not set duty cycle after " + std::to_string(max_retries) + " retries");
}

void RoboClawMotor::speed(int value) {
    if (!this->enabled) {
        return;
    }
    unsigned int counts_per_second = constrain(value, -2147483647, 2147483647);

    const int max_retries = 4;
    for (int retries = 0; retries < max_retries; ++retries) {
        bool success = this->motor_number == 1 ? this->roboclaw->SpeedM1(counts_per_second) : this->roboclaw->SpeedM2(counts_per_second);
        if (success) {
            return;
        }
    }
    throw std::runtime_error("could not set speed after " + std::to_string(max_retries) + " retries");
}

void RoboClawMotor::enable() {
    this->enabled = true;
    this->properties.at("enabled")->boolean_value = true;
}

void RoboClawMotor::disable() {
    this->speed(0);
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}

void RoboClawMotor::stop() {
    this->speed(0);
}

void RoboClawMotor::set_limits(int32_t low, int32_t high) {
    if (low >= high) {
        throw std::runtime_error("limit_low must be less than limit_high");
    }
    this->properties.at("limit_low")->integer_value = low;
    this->properties.at("limit_high")->integer_value = high;
    this->calibrated = true;
    this->properties.at("calibrated")->boolean_value = true;
}

bool RoboClawMotor::is_calibrated() const {
    return this->calibrated;
}

int32_t RoboClawMotor::get_limit_low() const {
    return static_cast<int32_t>(this->properties.at("limit_low")->integer_value);
}

int32_t RoboClawMotor::get_limit_high() const {
    return static_cast<int32_t>(this->properties.at("limit_high")->integer_value);
}

void RoboClawMotor::position(int32_t target, uint32_t speed, uint32_t accel, uint32_t deccel) {
    if (!this->enabled) {
        return;
    }
    if (!this->calibrated) {
        throw std::runtime_error("motor is not calibrated, call set_limits(low, high) first");
    }
    const int32_t low = this->get_limit_low();
    const int32_t high = this->get_limit_high();
    target = constrain(target, low, high);

    const int max_retries = 4;
    for (int retries = 0; retries < max_retries; ++retries) {
        bool success = this->motor_number == 1
                           ? this->roboclaw->SpeedAccelDeccelPositionM1(accel, speed, deccel, target, 0)
                           : this->roboclaw->SpeedAccelDeccelPositionM2(accel, speed, deccel, target, 0);
        if (success) {
            return;
        }
    }
    throw std::runtime_error("could not set position after " + std::to_string(max_retries) + " retries");
}
