#include "innotronic_delta_arm.h"
#include "../utils/uart.h"
#include <cmath>
#include <stdexcept>

static constexpr double VELOCITY_ACTIVE_THRESHOLD = 0.01; // rad/s, 1 raw digit = 0.01 rad/s

REGISTER_MODULE_DEFAULTS(InnotronicDeltaArm)

const std::map<std::string, Variable_ptr> InnotronicDeltaArm::get_defaults() {
    return {
        {"enabled", std::make_shared<BooleanVariable>(true)},
        {"calibrating", std::make_shared<BooleanVariable>(false)},
        {"calibrated_left", std::make_shared<BooleanVariable>(false)},
        {"calibrated_right", std::make_shared<BooleanVariable>(false)},
        {"cal_speed", std::make_shared<IntegerVariable>(20)},
    };
}

InnotronicDeltaArm::InnotronicDeltaArm(const std::string name, const InnotronicMotor_ptr motor, const Input_ptr left_endstop, const Input_ptr right_endstop)
    : Module(innotronic_delta_arm, name), motor(motor), left_endstop(left_endstop), right_endstop(right_endstop) {
    this->properties = InnotronicDeltaArm::get_defaults();
}

bool InnotronicDeltaArm::is_motor_active(bool left) const {
    double vel = this->motor->get_property(left ? "angular_vel_m1" : "angular_vel_m2")->number_value;
    return std::abs(vel) >= VELOCITY_ACTIVE_THRESHOLD;
}

bool InnotronicDeltaArm::can_move(int16_t left_ticks, int16_t right_ticks) const {
    if (!this->enabled) {
        return false;
    }
    bool left_endstop_active = this->left_endstop && this->left_endstop->get_property("active")->boolean_value;
    bool right_endstop_active = this->right_endstop && this->right_endstop->get_property("active")->boolean_value;
    if (left_endstop_active && left_ticks > 0) {
        echo("%s: left endstop triggered, blocking positive motion", this->name.c_str());
        return false;
    }
    if (right_endstop_active && right_ticks < 0) {
        echo("%s: right endstop triggered, blocking negative motion", this->name.c_str());
        return false;
    }
    return true;
}

void InnotronicDeltaArm::start_reference(const std::string &side) {
    if (this->cal_state != cal_idle) {
        echo("%s: already calibrating, ignoring reference(%s)", this->name.c_str(), side.c_str());
        return;
    }
    if (side == "left") {
        this->cal_state = cal_left;
        this->properties.at("calibrating")->boolean_value = true;
        this->motor->reference_drive_start(1, true);
        echo("%s: reference left started", this->name.c_str());
    } else if (side == "right") {
        this->cal_state = cal_right;
        this->properties.at("calibrating")->boolean_value = true;
        this->motor->reference_drive_start(2, true);
        echo("%s: reference right started", this->name.c_str());
    } else if (side == "both") {
        this->cal_state = cal_both;
        this->both_left_done = false;
        this->both_right_done = false;
        this->properties.at("calibrating")->boolean_value = true;
        this->motor->reference_drive_start(1, true);
        this->motor->reference_drive_start(2, true);
        echo("%s: reference both started", this->name.c_str());
    } else {
        throw std::runtime_error("reference side must be \"left\", \"right\" or \"both\"");
    }
}

void InnotronicDeltaArm::step() {
    if (this->properties.at("enabled")->boolean_value != this->enabled) {
        if (this->properties.at("enabled")->boolean_value) {
            this->enable();
        } else {
            this->disable();
        }
    }

    bool left_endstop_active = this->left_endstop && this->left_endstop->get_property("active")->boolean_value;
    bool right_endstop_active = this->right_endstop && this->right_endstop->get_property("active")->boolean_value;

    // Brake motor when endstop triggers and motor is drawing current
    if (left_endstop_active && this->is_motor_active(true)) {
        this->motor->reference_drive_stop(1);
        echo("%s: left endstop triggered, braking left motor", this->name.c_str());
    }
    if (right_endstop_active && this->is_motor_active(false)) {
        this->motor->reference_drive_stop(2);
        echo("%s: right endstop triggered, braking right motor", this->name.c_str());
    }

    // Calibration state machine
    switch (this->cal_state) {
    case cal_left:
        if (left_endstop_active) {
            // #PLACEHOLDER - ref_stop configure command for motor 1, setting_id TBD from Innotronic
            echo("%s: left endstop reached, sending ref_stop(1)", this->name.c_str());
            this->cal_state = cal_verify_left;
        }
        break;
    case cal_verify_left: {
        double angle = this->motor->get_property("angle_m1")->number_value;
        echo("%s: left reference verify, angle=%.4f", this->name.c_str(), angle);
        this->properties.at("calibrated_left")->boolean_value = true;
        this->properties.at("calibrating")->boolean_value = false;
        this->cal_state = cal_idle;
        echo("%s: left calibration complete", this->name.c_str());
    } break;
    case cal_right:
        if (right_endstop_active) {
            // #PLACEHOLDER - ref_stop configure command for motor 2, setting_id TBD from Innotronic
            echo("%s: right endstop reached, sending ref_stop(2)", this->name.c_str());
            this->cal_state = cal_verify_right;
        }
        break;
    case cal_verify_right: {
        double angle = this->motor->get_property("angle_m2")->number_value;
        echo("%s: right reference verify, angle=%.4f", this->name.c_str(), angle);
        this->properties.at("calibrated_right")->boolean_value = true;
        this->properties.at("calibrating")->boolean_value = false;
        this->cal_state = cal_idle;
        echo("%s: right calibration complete", this->name.c_str());
    } break;
    case cal_both:
        if (left_endstop_active && !this->both_left_done) {
            this->motor->reference_drive_stop(1);
            this->both_left_done = true;
            echo("%s: left endstop reached during both-reference", this->name.c_str());
        }
        if (right_endstop_active && !this->both_right_done) {
            this->motor->reference_drive_stop(2);
            this->both_right_done = true;
            echo("%s: right endstop reached during both-reference", this->name.c_str());
        }
        if (this->both_left_done && this->both_right_done) {
            this->cal_state = cal_verify_both;
        }
        break;
    case cal_verify_both: {
        double angle_m1 = this->motor->get_property("angle_m1")->number_value;
        double angle_m2 = this->motor->get_property("angle_m2")->number_value;
        echo("%s: both reference verify, angle_m1=%.4f angle_m2=%.4f", this->name.c_str(), angle_m1, angle_m2);
        this->properties.at("calibrated_left")->boolean_value = true;
        this->properties.at("calibrated_right")->boolean_value = true;
        this->properties.at("calibrating")->boolean_value = false;
        this->cal_state = cal_idle;
        echo("%s: both calibration complete", this->name.c_str());
    } break;
    case cal_idle:
        break;
    }

    Module::step();
}

void InnotronicDeltaArm::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "position") {
        // position(left_ticks, right_ticks, [speed_left, speed_right])
        if (arguments.size() < 2 || arguments.size() > 4) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, integer, integer, integer, integer);
        int16_t left_ticks = static_cast<int16_t>(arguments[0]->evaluate_integer());
        int16_t right_ticks = static_cast<int16_t>(arguments[1]->evaluate_integer());
        uint16_t speed_left = arguments.size() > 2 ? static_cast<uint16_t>(arguments[2]->evaluate_integer()) : 0xFFFF;
        uint16_t speed_right = arguments.size() > 3 ? static_cast<uint16_t>(arguments[3]->evaluate_integer()) : 0xFFFF;
        if (this->can_move(left_ticks, right_ticks)) {
            this->motor->send_delta_angle_cmd(0x10, left_ticks, speed_left);
            this->motor->send_delta_angle_cmd(0x20, right_ticks, speed_right);
        }
    } else if (method_name == "move_a") {
        // move_a: left down (-80), right up (80), speed 10
        Module::expect(arguments, 0);
        this->motor->send_delta_angle_cmd(0x10, -80, 10);
        this->motor->send_delta_angle_cmd(0x20, 80, 10);
    } else if (method_name == "move_b") {
        // move_b: left up (10), right down (-10), speed 10
        Module::expect(arguments, 0);
        this->motor->send_delta_angle_cmd(0x10, -10, 10);
        this->motor->send_delta_angle_cmd(0x20, 10, 10);
    } else if (method_name == "reference") {
        Module::expect(arguments, 1, string);
        std::string side = arguments[0]->evaluate_string();
        this->start_reference(side);
    } else if (method_name == "stop") {
        // stop() = stop both, stop(1) or stop(2) = stop individual motor
        if (arguments.size() == 0) {
            if (this->cal_state != cal_idle) {
                this->cal_state = cal_idle;
                this->properties.at("calibrating")->boolean_value = false;
                echo("%s: calibration aborted", this->name.c_str());
            }
            this->motor->stop();
        } else {
            Module::expect(arguments, 1, integer);
            int motor_nr = arguments[0]->evaluate_integer();
            if (motor_nr < 1 || motor_nr > 2) {
                throw std::runtime_error("motor number must be 1 or 2");
            }
            this->motor->reference_drive_stop(motor_nr);
            echo("%s: brake motor %d", this->name.c_str(), motor_nr);
        }
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->motor->disable();
    } else if (method_name == "on") {
        Module::expect(arguments, 0);
        this->motor->enable();
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

void InnotronicDeltaArm::enable() {
    this->enabled = true;
    this->properties.at("enabled")->boolean_value = true;
    this->motor->enable();
}

void InnotronicDeltaArm::disable() {
    this->motor->stop();
    this->motor->disable();
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}
