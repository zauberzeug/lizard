#include "innotronic_delta_arm.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include <cmath>
#include <stdexcept>

// Detection-tuning constants — fixed enough that runtime tuning isn't needed.
static constexpr double POSITION_TOL_DEG = 1.2;     // "target reached" tolerance
static constexpr unsigned long STABLE_MS = 100;     // hold time inside tolerance before active=false
static constexpr double STALL_POS_TOL_DEG = 2.4;    // position drift allowed within stall window
static constexpr unsigned long STALL_MS = 200;      // overcurrent + no movement → stall
static constexpr int16_t BACKOFF_STEP_TICKS = 10;
static constexpr uint8_t BACKOFF_SPEED = 20;
static constexpr unsigned long BACKOFF_INTERVAL_MS = 200;

REGISTER_MODULE_DEFAULTS(InnotronicDeltaArm)

const std::map<std::string, Variable_ptr> InnotronicDeltaArm::get_defaults() {
    return {
        {"enabled", std::make_shared<BooleanVariable>(true)},
        {"calibrating", std::make_shared<BooleanVariable>(false)},
        {"calibrated_left", std::make_shared<BooleanVariable>(false)},
        {"calibrated_right", std::make_shared<BooleanVariable>(false)},
        {"active", std::make_shared<BooleanVariable>(false)},
        {"stalled", std::make_shared<BooleanVariable>(false)},
        {"angle_left", std::make_shared<NumberVariable>()},
        {"angle_right", std::make_shared<NumberVariable>()},
        {"deg_limit", std::make_shared<NumberVariable>(60.0)},
        {"cal_timeout", std::make_shared<NumberVariable>(10.0)},
        {"stall_current", std::make_shared<NumberVariable>(3.0)},
    };
}

InnotronicDeltaArm::InnotronicDeltaArm(const std::string name, const InnotronicDeltaMotor_ptr motor,
                                       const Input_ptr left_endstop, const Input_ptr right_endstop)
    : Module(innotronic_delta_arm, name), motor(motor), left_endstop(left_endstop), right_endstop(right_endstop),
      deg_per_tick(360.0 / motor->motor_ticks) {
    this->properties = InnotronicDeltaArm::get_defaults();
}

bool InnotronicDeltaArm::is_enabled() const {
    return this->properties.at("enabled")->boolean_value;
}

bool InnotronicDeltaArm::is_calibrated() const {
    return this->properties.at("calibrated_left")->boolean_value &&
           this->properties.at("calibrated_right")->boolean_value;
}

bool InnotronicDeltaArm::endstop_active(const Input_ptr &input) const {
    return input && input->get_property("active")->boolean_value;
}

void InnotronicDeltaArm::move_to(double left_deg, double right_deg, uint8_t speed_left, uint8_t speed_right) {
    if (!this->is_calibrated()) {
        echo("%s: not calibrated, ignoring move command", this->name.c_str());
        return;
    }
    if (!this->can_move(left_deg, right_deg)) {
        return;
    }
    int16_t left_ticks = static_cast<int16_t>(left_deg / this->deg_per_tick);
    int16_t right_ticks = static_cast<int16_t>(right_deg / this->deg_per_tick);
    this->motor->send_delta_angle_cmd(0x30, left_ticks, speed_left, right_ticks, speed_right);
    this->target_left_deg = left_deg;
    this->target_right_deg = right_deg;
    this->was_in_tol = false;
    this->stable_since = 0;
    this->was_stalling = false;
    this->stall_since = 0;
    this->properties.at("stalled")->boolean_value = false;
    this->properties.at("active")->boolean_value = true;
}

bool InnotronicDeltaArm::can_move(double left_deg, double right_deg) const {
    if (!this->is_enabled()) {
        return false;
    }
    if (this->endstop_active(this->left_endstop) && left_deg > 0) {
        echo("%s: left endstop triggered, blocking positive motion", this->name.c_str());
        return false;
    }
    if (this->endstop_active(this->right_endstop) && right_deg < 0) {
        echo("%s: right endstop triggered, blocking negative motion", this->name.c_str());
        return false;
    }
    const double deg_limit = this->properties.at("deg_limit")->number_value;
    if (deg_limit > 0) {
        if (left_deg > 0 || left_deg < -deg_limit) {
            echo("%s: left motor target %.2f° out of range [-%.2f, 0]",
                 this->name.c_str(), left_deg, deg_limit);
            return false;
        }
        if (right_deg < 0 || right_deg > deg_limit) {
            echo("%s: right motor target %.2f° out of range [0, %.2f]",
                 this->name.c_str(), right_deg, deg_limit);
            return false;
        }
    }
    return true;
}

void InnotronicDeltaArm::start_reference(const std::string &side) {
    if (!this->is_enabled()) {
        echo("%s: not enabled, ignoring reference(%s)", this->name.c_str(), side.c_str());
        return;
    }
    if (this->cal_state != cal_idle) {
        echo("%s: already calibrating, ignoring reference(%s)", this->name.c_str(), side.c_str());
        return;
    }
    if (side != "left" && side != "right" && side != "both") {
        throw std::runtime_error("reference side must be \"left\", \"right\" or \"both\"");
    }
    // Reset ref results so stale values from previous runs don't cause immediate abort.
    this->motor->get_property("ref_result_m1")->integer_value = 0;
    this->motor->get_property("ref_result_m2")->integer_value = 0;
    this->last_ref_m1 = 0;
    this->last_ref_m2 = 0;
    this->m1_brake_sent = false;
    this->m2_brake_sent = false;
    this->cal_started_at = millis();
    this->properties.at("active")->boolean_value = false;
    this->properties.at("stalled")->boolean_value = false;
    this->was_stalling = false;

    // If the relevant endstop is already active, nudge the motor away first.
    // The reference drive would otherwise immediately re-brake and fail (overcurrent).
    bool left_active = this->endstop_active(this->left_endstop);
    bool right_active = this->endstop_active(this->right_endstop);
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

    this->properties.at("calibrating")->boolean_value = true;
    if (side == "left") {
        this->cal_state = cal_left;
        this->motor->reference_drive_start(1, true);
        echo("%s: reference left started", this->name.c_str());
    } else if (side == "right") {
        this->cal_state = cal_right;
        this->motor->reference_drive_start(2, false);
        echo("%s: reference right started", this->name.c_str());
    } else {
        this->cal_state = cal_both;
        this->both_left_done = false;
        this->both_right_done = false;
        this->motor->reference_drive_start(1, true);
        this->motor->reference_drive_start(2, false);
        echo("%s: reference both started", this->name.c_str());
    }
}

void InnotronicDeltaArm::step() {
    bool desired = this->is_enabled();
    if (desired != this->last_applied_enabled) {
        if (desired) {
            this->enable();
        } else {
            this->disable();
        }
    }

    bool left_endstop_active = this->endstop_active(this->left_endstop);
    bool right_endstop_active = this->endstop_active(this->right_endstop);

    // Export current motor angles in degrees (delta mode: 0x15 reports angle_mX as int16 ticks).
    double cur_m1 = this->motor->get_property("angle_m1")->number_value;
    double cur_m2 = this->motor->get_property("angle_m2")->number_value;
    double cur_l_deg = cur_m1 * this->deg_per_tick;
    double cur_r_deg = cur_m2 * this->deg_per_tick;
    this->properties.at("angle_left")->number_value = cur_l_deg;
    this->properties.at("angle_right")->number_value = cur_r_deg;

    // Active / target-reached tracking (no velocity feedback in delta mode).
    if (this->properties.at("active")->boolean_value) {
        bool in_tol = std::abs(cur_l_deg - this->target_left_deg) <= POSITION_TOL_DEG &&
                      std::abs(cur_r_deg - this->target_right_deg) <= POSITION_TOL_DEG;
        if (in_tol) {
            if (!this->was_in_tol) {
                this->stable_since = millis();
                this->was_in_tol = true;
            } else if (millis_since(this->stable_since) >= STABLE_MS) {
                this->properties.at("active")->boolean_value = false;
                this->was_in_tol = false;
            }
        } else {
            this->was_in_tol = false;
        }
    }

    // Endstop safety: brake once on rising edge while a user move is active.
    if (this->properties.at("active")->boolean_value) {
        if (left_endstop_active && !this->left_endstop_prev) {
            this->motor->reference_drive_stop(1);
            this->properties.at("active")->boolean_value = false;
            echo("%s: left endstop triggered during move", this->name.c_str());
        }
        if (right_endstop_active && !this->right_endstop_prev) {
            this->motor->reference_drive_stop(2);
            this->properties.at("active")->boolean_value = false;
            echo("%s: right endstop triggered during move", this->name.c_str());
        }
    }
    this->left_endstop_prev = left_endstop_active;
    this->right_endstop_prev = right_endstop_active;

    // Stall guard: overcurrent AND position not moving.
    // High current while moving = legitimate load; high current with no progress = real stall.
    if (this->properties.at("active")->boolean_value && this->cal_state == cal_idle) {
        const double i_max = this->properties.at("stall_current")->number_value;
        const double i_m1 = this->motor->get_property("current_m1")->number_value;
        const double i_m2 = this->motor->get_property("current_m2")->number_value;
        const bool overcurrent = std::abs(i_m1) > i_max || std::abs(i_m2) > i_max;
        if (overcurrent) {
            if (!this->was_stalling) {
                this->stall_since = millis();
                this->stall_start_deg_m1 = cur_l_deg;
                this->stall_start_deg_m2 = cur_r_deg;
                this->was_stalling = true;
            } else if (std::abs(cur_l_deg - this->stall_start_deg_m1) > STALL_POS_TOL_DEG ||
                       std::abs(cur_r_deg - this->stall_start_deg_m2) > STALL_POS_TOL_DEG) {
                // Motor still moving despite overcurrent — restart the window.
                this->stall_since = millis();
                this->stall_start_deg_m1 = cur_l_deg;
                this->stall_start_deg_m2 = cur_r_deg;
            } else if (millis_since(this->stall_since) >= STALL_MS) {
                this->properties.at("stalled")->boolean_value = true;
                // Position drifts on forced stop — invalidate calibration so a
                // recovery requires an explicit reference drive.
                this->properties.at("calibrated_left")->boolean_value = false;
                this->properties.at("calibrated_right")->boolean_value = false;
                this->was_stalling = false;
                echo("%s: stall detected (i_m1=%.2fA i_m2=%.2fA > %.2fA, no position change) — motor off, recalibrate",
                     this->name.c_str(), i_m1, i_m2, i_max);
                this->disable();
            }
        } else {
            this->was_stalling = false;
        }
    }

    // Latch ref results: 0x14 arrives per-motor in separate messages, each only
    // setting its own nibble (the other is NONE/0). Keep the highest non-zero seen.
    int ref_m1 = this->motor->get_property("ref_result_m1")->integer_value;
    int ref_m2 = this->motor->get_property("ref_result_m2")->integer_value;
    if (ref_m1 != 0) {
        this->last_ref_m1 = ref_m1;
    }
    if (ref_m2 != 0) {
        this->last_ref_m2 = ref_m2;
    }

    // Calibration timeout: brake both motors and abort if too long.
    if (this->cal_state != cal_idle) {
        const double timeout_s = this->properties.at("cal_timeout")->number_value;
        if (timeout_s > 0 && millis_since(this->cal_started_at) > static_cast<unsigned long>(timeout_s * 1000)) {
            this->motor->reference_drive_stop(1);
            this->motor->reference_drive_stop(2);
            this->properties.at("calibrating")->boolean_value = false;
            this->properties.at("calibrated_left")->boolean_value = false;
            this->properties.at("calibrated_right")->boolean_value = false;
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
                echo("%s: left endstop reached during both-reference (ref_result=%d)",
                     this->name.c_str(), this->last_ref_m1);
                this->both_left_done = true;
            } else if (left_endstop_active && !this->m1_brake_sent) {
                this->motor->reference_drive_stop(1);
                this->m1_brake_sent = true;
            }
        }
        if (!this->both_right_done) {
            if (this->last_ref_m2 != 0) {
                echo("%s: right endstop reached during both-reference (ref_result=%d)",
                     this->name.c_str(), this->last_ref_m2);
                this->both_right_done = true;
            } else if (right_endstop_active && !this->m2_brake_sent) {
                this->motor->reference_drive_stop(2);
                this->m2_brake_sent = true;
            }
        }
        if (this->both_left_done && this->both_right_done) {
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
        // Back off a few ticks in the operating-range direction (negative for m1, positive for m2).
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
        if (millis_since(this->last_backoff_at) >= BACKOFF_INTERVAL_MS) {
            bool nudge_left = need_left && left_endstop_active;
            bool nudge_right = need_right && right_endstop_active;
            // Only one motor per tick — alternate when both endstops are still active.
            bool pick_left = nudge_left && (!nudge_right || !this->backoff_last_was_left);
            if (pick_left) {
                int16_t a1 = static_cast<int16_t>(cur_m1);
                int16_t target_m1 = a1 - BACKOFF_STEP_TICKS;
                echo("%s: backoff left angle=%d -> %d", this->name.c_str(), a1, target_m1);
                this->motor->send_delta_angle_cmd(0x10, target_m1, BACKOFF_SPEED);
                this->backoff_last_was_left = true;
            } else if (nudge_right) {
                int16_t a2 = static_cast<int16_t>(cur_m2);
                int16_t target_m2 = a2 + BACKOFF_STEP_TICKS;
                echo("%s: backoff right angle=%d -> %d", this->name.c_str(), a2, target_m2);
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

    Module::step();
}

void InnotronicDeltaArm::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "position") {
        if (arguments.size() < 2 || arguments.size() > 4) {
            throw std::runtime_error("unexpected number of arguments");
        }
        double left_deg = arguments[0]->evaluate_number();
        double right_deg = arguments[1]->evaluate_number();
        uint8_t speed_left = arguments.size() > 2 ? static_cast<uint8_t>(arguments[2]->evaluate_integer()) : 10;
        uint8_t speed_right = arguments.size() > 3 ? static_cast<uint8_t>(arguments[3]->evaluate_integer()) : 10;
        this->move_to(left_deg, right_deg, speed_left, speed_right);
    } else if (method_name == "reference") {
        Module::expect(arguments, 1, string);
        this->start_reference(arguments[0]->evaluate_string());
    } else if (method_name == "stop") {
        // stop() = stop both, stop(1) or stop(2) = brake individual motor
        if (arguments.size() == 0) {
            if (this->cal_state != cal_idle) {
                this->cal_state = cal_idle;
                this->properties.at("calibrating")->boolean_value = false;
                echo("%s: calibration aborted", this->name.c_str());
            }
            this->motor->stop();
            this->properties.at("active")->boolean_value = false;
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
    this->properties.at("enabled")->boolean_value = true;
    this->last_applied_enabled = true;
    this->motor->enable();
}

void InnotronicDeltaArm::disable() {
    this->motor->stop();
    this->motor->disable();
    this->properties.at("enabled")->boolean_value = false;
    this->last_applied_enabled = false;
    this->properties.at("active")->boolean_value = false;
}
