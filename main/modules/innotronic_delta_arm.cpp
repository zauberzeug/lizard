#include "innotronic_delta_arm.h"
#include "../utils/uart.h"
#include <cmath>
#include <stdexcept>

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

bool InnotronicDeltaArm::can_move(float angle_a, float angle_b) const {
    if (!this->enabled) {
        return false;
    }
    bool left_active = this->left_endstop && this->left_endstop->get_property("active")->boolean_value;
    bool right_active = this->right_endstop && this->right_endstop->get_property("active")->boolean_value;
    if (left_active && angle_a > 0) {
        echo("%s: left endstop triggered, blocking positive motion on A", this->name.c_str());
        return false;
    }
    if (right_active && angle_b < 0) {
        echo("%s: right endstop triggered, blocking negative motion on B", this->name.c_str());
        return false;
    }
    return true;
}

void InnotronicDeltaArm::start_reference(const std::string &side) {
    if (this->cal_state != cal_idle) {
        echo("%s: already calibrating, ignoring reference(%s)", this->name.c_str(), side.c_str());
        return;
    }
    uint8_t cal_speed = static_cast<uint8_t>(this->properties.at("cal_speed")->integer_value);
    if (side == "left") {
        this->cal_state = cal_left;
        this->properties.at("calibrating")->boolean_value = true;
        // Move left arm (A) toward endstop (positive direction), keep right (B) still (vel=0)
        this->motor->send_delta_angle_cmd(2 * M_PI, 0, cal_speed, 0);
        echo("%s: reference left started, moving A toward endstop", this->name.c_str());
    } else if (side == "right") {
        this->cal_state = cal_right;
        this->properties.at("calibrating")->boolean_value = true;
        // Move right arm (B) toward endstop (negative direction), keep left (A) still (vel=0)
        this->motor->send_delta_angle_cmd(0, -2 * M_PI, 0, cal_speed);
        echo("%s: reference right started, moving B toward endstop", this->name.c_str());
    } else {
        throw std::runtime_error("reference side must be \"left\" or \"right\"");
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

    bool left_active = this->left_endstop && this->left_endstop->get_property("active")->boolean_value;
    bool right_active = this->right_endstop && this->right_endstop->get_property("active")->boolean_value;

    switch (this->cal_state) {
    case cal_left:
        if (left_active) {
            // Endstop hit: stop motor A, send ref_stop to zero it
            // #PLACEHOLDER - per-motor stop for motor 1
            this->motor->stop();
            echo("%s: left endstop reached, sending ref_stop(1)", this->name.c_str());
            // #PLACEHOLDER - ref_stop configure command for motor 1, setting_id TBD from Innotronic
            this->cal_state = cal_verify_left;
        }
        break;
    case cal_verify_left:
        // Verify angle is ~0 after ref_stop (one cycle later so 0x12 has updated)
        {
            double angle = this->motor->get_property("angle")->number_value;
            echo("%s: left reference verify, angle=%.4f", this->name.c_str(), angle);
            this->properties.at("calibrated_left")->boolean_value = true;
            this->properties.at("calibrating")->boolean_value = false;
            this->cal_state = cal_idle;
            echo("%s: left calibration complete", this->name.c_str());
        }
        break;
    case cal_right:
        if (right_active) {
            // Endstop hit: stop motor B, send ref_stop to zero it
            // #PLACEHOLDER - per-motor stop for motor 2
            this->motor->stop();
            echo("%s: right endstop reached, sending ref_stop(2)", this->name.c_str());
            // #PLACEHOLDER - ref_stop configure command for motor 2, setting_id TBD from Innotronic
            this->cal_state = cal_verify_right;
        }
        break;
    case cal_verify_right:
        // Verify angle is ~0 after ref_stop (one cycle later so 0x12 has updated)
        {
            double angle = this->motor->get_property("angle")->number_value;
            echo("%s: right reference verify, angle=%.4f", this->name.c_str(), angle);
            this->properties.at("calibrated_right")->boolean_value = true;
            this->properties.at("calibrating")->boolean_value = false;
            this->cal_state = cal_idle;
            echo("%s: right calibration complete", this->name.c_str());
        }
        break;
    case cal_idle:
        break;
    }

    Module::step();
}

void InnotronicDeltaArm::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "position") {
        if (arguments.size() < 2 || arguments.size() > 6) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery, numbery, numbery, numbery, numbery);
        float angle_a = arguments[0]->evaluate_number();
        float angle_b = arguments[1]->evaluate_number();
        if (this->can_move(angle_a, angle_b)) {
            uint8_t vel_lim_a = arguments.size() > 2 ? static_cast<uint8_t>(arguments[2]->evaluate_number()) : 0xFF;
            uint8_t vel_lim_b = arguments.size() > 3 ? static_cast<uint8_t>(arguments[3]->evaluate_number()) : 0xFF;
            uint8_t acc_lim = arguments.size() > 4 ? static_cast<uint8_t>(arguments[4]->evaluate_number()) : 0xFF;
            int8_t jerk_lim_exp = arguments.size() > 5 ? static_cast<int8_t>(arguments[5]->evaluate_number()) : (int8_t)0xFF;
            this->motor->send_delta_angle_cmd(angle_a, angle_b, vel_lim_a, vel_lim_b, acc_lim, jerk_lim_exp);
        }
    } else if (method_name == "reference") {
        Module::expect(arguments, 1, string);
        std::string side = arguments[0]->evaluate_string();
        this->start_reference(side);
    } else if (method_name == "ref_stop") {
        Module::expect(arguments, 1, integer);
        int motor_nr = arguments[0]->evaluate_integer();
        if (motor_nr < 1 || motor_nr > 2) {
            throw std::runtime_error("motor number must be 1 or 2");
        }
        // #PLACEHOLDER - configure command for HALL_REACHED zeroing per motor, setting_id TBD from Innotronic
        echo("%s: ref_stop motor %d (PLACEHOLDER - HALL_REACHED configure TBD)", this->name.c_str(), motor_nr);
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
            // #PLACEHOLDER - per-motor stop in delta mode, TBD from Innotronic
            echo("%s: stop motor %d (PLACEHOLDER - per-motor stop TBD)", this->name.c_str(), motor_nr);
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
