#include "innotronic_delta_arm.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include <cmath>
#include <stdexcept>

static constexpr double VELOCITY_ACTIVE_THRESHOLD = 0.01; // rad/s, 1 raw digit = 0.01 rad/s

static constexpr int REF_OK = 1;

REGISTER_MODULE_DEFAULTS(InnotronicDeltaArm)

const std::map<std::string, Variable_ptr> InnotronicDeltaArm::get_defaults() {
    return {
        {"enabled", std::make_shared<BooleanVariable>(true)},
        {"calibrating", std::make_shared<BooleanVariable>(false)},
        {"calibrated_left", std::make_shared<BooleanVariable>(false)},
        {"calibrated_right", std::make_shared<BooleanVariable>(false)},
        {"cal_timeout", std::make_shared<NumberVariable>(10.0)},
        {"tick_limit", std::make_shared<IntegerVariable>(5)},
        {"loop", std::make_shared<BooleanVariable>(false)},
        {"loop_speed", std::make_shared<IntegerVariable>(10)},
        {"loop_interval", std::make_shared<NumberVariable>(3.0)},
        {"loop_top", std::make_shared<IntegerVariable>(20)},
        {"loop_bottom", std::make_shared<IntegerVariable>(50)},
        {"loop_spread", std::make_shared<IntegerVariable>(40)},
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

bool InnotronicDeltaArm::is_calibrated() const {
    return this->properties.at("calibrated_left")->boolean_value &&
           this->properties.at("calibrated_right")->boolean_value;
}

void InnotronicDeltaArm::move_to(int16_t left_ticks, int16_t right_ticks, uint8_t speed_left, uint8_t speed_right) {
    if (!this->is_calibrated()) {
        echo("%s: not calibrated, ignoring move command", this->name.c_str());
        return;
    }
    if (this->can_move(left_ticks, right_ticks)) {
        this->motor->send_delta_angle_cmd(0x30, left_ticks, speed_left, right_ticks, speed_right);
    }
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
    int tick_limit = this->properties.at("tick_limit")->integer_value;
    if (tick_limit > 0) {
        if (left_ticks > 0 || left_ticks < -tick_limit) {
            echo("%s: left motor target %d out of range [-%d, 0]",
                 this->name.c_str(), left_ticks, tick_limit);
            return false;
        }
        if (right_ticks < 0 || right_ticks > tick_limit) {
            echo("%s: right motor target %d out of range [0, %d]",
                 this->name.c_str(), right_ticks, tick_limit);
            return false;
        }
    }
    return true;
}

void InnotronicDeltaArm::start_reference(const std::string &side) {
    if (this->cal_state != cal_idle) {
        echo("%s: already calibrating, ignoring reference(%s)", this->name.c_str(), side.c_str());
        return;
    }
    if (side != "left" && side != "right" && side != "both") {
        throw std::runtime_error("reference side must be \"left\", \"right\" or \"both\"");
    }
    // Reset ref results so stale values from previous runs don't cause immediate abort
    this->motor->get_property("ref_result_m1")->integer_value = 0;
    this->motor->get_property("ref_result_m2")->integer_value = 0;
    this->last_ref_m1 = 0;
    this->last_ref_m2 = 0;
    this->m1_brake_sent = false;
    this->m2_brake_sent = false;
    this->cal_started_at = millis();

    // If the relevant endstop is already active, nudge the motor away first.
    // The reference drive would otherwise immediately re-brake and fail (overcurrent).
    bool left_active = this->left_endstop && this->left_endstop->get_property("active")->boolean_value;
    bool right_active = this->right_endstop && this->right_endstop->get_property("active")->boolean_value;
    bool needs_left_backoff = (side == "left" || side == "both") && left_active;
    bool needs_right_backoff = (side == "right" || side == "both") && right_active;
    if (needs_left_backoff || needs_right_backoff) {
        this->cal_state = cal_backoff;
        this->pending_ref_side = side;
        this->last_backoff_at = 0;
        this->properties.at("calibrating")->boolean_value = true;
        echo("%s: endstop active, backing off before reference %s", this->name.c_str(), side.c_str());
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
        this->motor->reference_drive_start(2, false);
        echo("%s: reference right started", this->name.c_str());
    } else { // "both"
        this->cal_state = cal_both;
        this->both_left_done = false;
        this->both_right_done = false;
        this->properties.at("calibrating")->boolean_value = true;
        this->motor->reference_drive_start(1, true);
        this->motor->reference_drive_start(2, false);
        echo("%s: reference both started", this->name.c_str());
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

    // Latch ref results: 0x14 arrives per-motor in separate messages,
    // each only setting its own nibble (the other is NONE/0).
    // We keep the highest non-zero result seen for each motor.
    int ref_m1 = this->motor->get_property("ref_result_m1")->integer_value;
    int ref_m2 = this->motor->get_property("ref_result_m2")->integer_value;
    if (ref_m1 != 0) {
        this->last_ref_m1 = ref_m1;
    }
    if (ref_m2 != 0) {
        this->last_ref_m2 = ref_m2;
    }

    // Calibration timeout: brake both motors and abort if we've been calibrating too long
    if (this->cal_state != cal_idle) {
        double timeout_s = this->properties.at("cal_timeout")->number_value;
        if (timeout_s > 0 && millis_since(this->cal_started_at) > static_cast<unsigned long>(timeout_s * 1000)) {
            this->motor->reference_drive_stop(1);
            this->motor->reference_drive_stop(2);
            this->properties.at("calibrating")->boolean_value = false;
            this->cal_state = cal_idle;
            echo("%s: calibration timeout after %.1fs", this->name.c_str(), timeout_s);
        }
    }

    // Calibration state machine — endstop only triggers the brake; "done" is driven
    // by the motor's own 0x14 ReferenceFeedback (last_ref_mX becoming non-zero).
    switch (this->cal_state) {
    case cal_left:
        if (this->last_ref_m1 != 0) {
            if (this->last_ref_m1 == REF_OK) {
                this->properties.at("calibrated_left")->boolean_value = true;
                echo("%s: left endstop reached, calibration complete", this->name.c_str());
            } else {
                echo("%s: left reference failed (ref_result=%d)", this->name.c_str(), this->last_ref_m1);
            }
            this->properties.at("calibrating")->boolean_value = false;
            this->cal_state = cal_idle;
        } else if (left_endstop_active && !this->m1_brake_sent) {
            this->motor->reference_drive_stop(1);
            this->m1_brake_sent = true;
            echo("%s: left endstop reached, waiting for motor confirmation", this->name.c_str());
        }
        break;
    case cal_right:
        if (this->last_ref_m2 != 0) {
            if (this->last_ref_m2 == REF_OK) {
                this->properties.at("calibrated_right")->boolean_value = true;
                echo("%s: right endstop reached, calibration complete", this->name.c_str());
            } else {
                echo("%s: right reference failed (ref_result=%d)", this->name.c_str(), this->last_ref_m2);
            }
            this->properties.at("calibrating")->boolean_value = false;
            this->cal_state = cal_idle;
        } else if (right_endstop_active && !this->m2_brake_sent) {
            this->motor->reference_drive_stop(2);
            this->m2_brake_sent = true;
            echo("%s: right endstop reached, waiting for motor confirmation", this->name.c_str());
        }
        break;
    case cal_both:
        if (!this->both_left_done) {
            if (this->last_ref_m1 != 0) {
                if (this->last_ref_m1 == REF_OK) {
                    echo("%s: left endstop reached during both-reference", this->name.c_str());
                } else {
                    echo("%s: left reference failed during both-reference (ref_result=%d)", this->name.c_str(), this->last_ref_m1);
                }
                this->both_left_done = true;
            } else if (left_endstop_active && !this->m1_brake_sent) {
                this->motor->reference_drive_stop(1);
                this->m1_brake_sent = true;
            }
        }
        if (!this->both_right_done) {
            if (this->last_ref_m2 != 0) {
                if (this->last_ref_m2 == REF_OK) {
                    echo("%s: right endstop reached during both-reference", this->name.c_str());
                } else {
                    echo("%s: right reference failed during both-reference (ref_result=%d)", this->name.c_str(), this->last_ref_m2);
                }
                this->both_right_done = true;
            } else if (right_endstop_active && !this->m2_brake_sent) {
                this->motor->reference_drive_stop(2);
                this->m2_brake_sent = true;
            }
        }
        if (this->both_left_done && this->both_right_done) {
            // Both sides resolved — mark calibration results directly
            if (this->last_ref_m1 == REF_OK) {
                this->properties.at("calibrated_left")->boolean_value = true;
            }
            if (this->last_ref_m2 == REF_OK) {
                this->properties.at("calibrated_right")->boolean_value = true;
            }
            this->properties.at("calibrating")->boolean_value = false;
            this->cal_state = cal_idle;
            echo("%s: both calibration done (m1=%d m2=%d)", this->name.c_str(), this->last_ref_m1, this->last_ref_m2);
        }
        break;
    case cal_backoff: {
        // Nudge motors away from their endstops before the real reference drive starts.
        // Left endstop is hit at positive angle_m1 → back off negative.
        // Right endstop is hit at negative angle_m2 → back off positive.
        bool need_left = (this->pending_ref_side == "left" || this->pending_ref_side == "both");
        bool need_right = (this->pending_ref_side == "right" || this->pending_ref_side == "both");
        bool left_clear = !need_left || !left_endstop_active;
        bool right_clear = !need_right || !right_endstop_active;
        if (left_clear && right_clear) {
            std::string side = this->pending_ref_side;
            this->pending_ref_side.clear();
            this->cal_state = cal_idle;
            this->properties.at("calibrating")->boolean_value = false;
            echo("%s: endstops cleared, starting reference %s", this->name.c_str(), side.c_str());
            this->start_reference(side);
            break;
        }
        if (millis_since(this->last_backoff_at) >= 200) {
            constexpr int16_t BACKOFF_STEP_TICKS = 3;
            constexpr uint8_t BACKOFF_SPEED = 30;
            bool nudge_left = need_left && left_endstop_active;
            bool nudge_right = need_right && right_endstop_active;
            // Only one motor per tick — alternate when both endstops are still active.
            bool pick_left = nudge_left && (!nudge_right || !this->backoff_last_was_left);
            if (pick_left) {
                int16_t target_m1 = static_cast<int16_t>(this->motor->get_property("angle_m1")->number_value) - BACKOFF_STEP_TICKS;
                this->motor->send_delta_angle_cmd(0x10, target_m1, BACKOFF_SPEED);
                this->backoff_last_was_left = true;
            } else if (nudge_right) {
                int16_t target_m2 = static_cast<int16_t>(this->motor->get_property("angle_m2")->number_value) + BACKOFF_STEP_TICKS;
                this->motor->send_delta_angle_cmd(0x20, target_m2, BACKOFF_SPEED);
                this->backoff_last_was_left = false;
            }
            this->last_backoff_at = millis();
        }
        break;
    }
    case cal_idle:
        break;
    }

    // Box loop test: cycle through 4 corners around a (unten) and b (oben)
    // Order: links von b → rechts von b → rechts von a → links von a
    if (this->properties.at("loop")->boolean_value && this->cal_state == cal_idle) {
        double interval_s = this->properties.at("loop_interval")->number_value;
        if (millis_since(this->last_loop_move_at) >= static_cast<unsigned long>(interval_s * 1000)) {
            int top = this->properties.at("loop_top")->integer_value;
            int bottom = this->properties.at("loop_bottom")->integer_value;
            int spread = this->properties.at("loop_spread")->integer_value;
            int16_t box[4][2] = {
                {static_cast<int16_t>(-top - spread), static_cast<int16_t>(top)},           // links von b
                {static_cast<int16_t>(-top), static_cast<int16_t>(top + spread)},           // rechts von b
                {static_cast<int16_t>(-bottom), static_cast<int16_t>(bottom + spread)},     // rechts von a
                {static_cast<int16_t>(-bottom - spread), static_cast<int16_t>(bottom)},     // links von a
            };
            int step = this->loop_step % 4;
            uint8_t speed = static_cast<uint8_t>(this->properties.at("loop_speed")->integer_value);
            this->move_to(box[step][0], box[step][1], speed, speed);
            this->loop_step++;
            this->last_loop_move_at = millis();
        }
    }

    Module::step();
}

void InnotronicDeltaArm::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "position") {
        if (arguments.size() < 2 || arguments.size() > 4) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, integer, integer, integer, integer);
        int16_t left_ticks = static_cast<int16_t>(arguments[0]->evaluate_integer());
        int16_t right_ticks = static_cast<int16_t>(arguments[1]->evaluate_integer());
        uint8_t speed_left = arguments.size() > 2 ? static_cast<uint8_t>(arguments[2]->evaluate_integer()) : 10;
        uint8_t speed_right = arguments.size() > 3 ? static_cast<uint8_t>(arguments[3]->evaluate_integer()) : 10;
        this->move_to(left_ticks, right_ticks, speed_left, speed_right);
    } else if (method_name == "move_a") {
        if (arguments.size() > 1) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, integer);
        uint8_t speed = arguments.size() > 0 ? static_cast<uint8_t>(arguments[0]->evaluate_integer()) : 10;
        this->move_to(-80, 80, speed, speed);
    } else if (method_name == "move_b") {
        if (arguments.size() > 1) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, integer);
        uint8_t speed = arguments.size() > 0 ? static_cast<uint8_t>(arguments[0]->evaluate_integer()) : 10;
        this->move_to(-10, 10, speed, speed);
    } else if (method_name == "move_c") {
        if (arguments.size() > 1) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, integer);
        uint8_t speed = arguments.size() > 0 ? static_cast<uint8_t>(arguments[0]->evaluate_integer()) : 10;
        this->move_to(-20, 50, speed, speed);
    } else if (method_name == "move_d") {
        if (arguments.size() > 1) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, integer);
        uint8_t speed = arguments.size() > 0 ? static_cast<uint8_t>(arguments[0]->evaluate_integer()) : 10;
        this->move_to(-50, 20, speed, speed);
    } else if (method_name == "reference") {
        Module::expect(arguments, 1, string);
        std::string side = arguments[0]->evaluate_string();
        this->properties.at("loop")->boolean_value = false;
        this->start_reference(side);
    } else if (method_name == "loop") {
        Module::expect(arguments, 1, boolean);
        bool enable_loop = arguments[0]->evaluate_boolean();
        if (enable_loop && !this->is_calibrated()) {
            echo("%s: not calibrated, cannot start loop", this->name.c_str());
            this->properties.at("loop")->boolean_value = false;
            return;
        }
        this->properties.at("loop")->boolean_value = enable_loop;
        if (enable_loop) {
            this->loop_step = 0;
            this->last_loop_move_at = 0;
            echo("%s: loop started", this->name.c_str());
        } else {
            echo("%s: loop stopped", this->name.c_str());
        }
    } else if (method_name == "stop") {
        // stop() = stop both, stop(1) or stop(2) = stop individual motor
        if (arguments.size() == 0) {
            this->properties.at("loop")->boolean_value = false;
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
    this->properties.at("loop")->boolean_value = false;
    this->motor->stop();
    this->motor->disable();
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}
