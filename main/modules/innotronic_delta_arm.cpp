#include "innotronic_delta_arm.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include <cmath>
#include <stdexcept>

// Detection-tuning constants — fixed enough that runtime tuning isn't needed.
static constexpr double POSITION_TOL_FACTOR = 1.5;  // tolerance multiplier applied to deg_per_tick
static constexpr unsigned long STABLE_MS = 100;     // hold time inside tolerance before state→0
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
        {"state", std::make_shared<IntegerVariable>(0)},
        {"stalled", std::make_shared<BooleanVariable>(false)},
        {"angle_left", std::make_shared<NumberVariable>()},
        {"angle_right", std::make_shared<NumberVariable>()},
        {"deg_limit", std::make_shared<NumberVariable>(60.0)},
        {"cal_timeout", std::make_shared<NumberVariable>(10.0)},
        {"stall_current", std::make_shared<NumberVariable>(3.0)},
        // Set true if motor channel 1 is wired to the physical right motor (and m2 to the left).
        // Channel-mapping flips for moves, references, backoff, angle/current reporting; the
        // CW-for-left / CCW-for-right reference directions stay bound to the physical side.
        {"motors_swapped", std::make_shared<BooleanVariable>(false)},
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

bool InnotronicDeltaArm::is_motors_swapped() const {
    return this->properties.at("motors_swapped")->boolean_value;
}

int InnotronicDeltaArm::channel_for_left() const {
    return this->is_motors_swapped() ? 2 : 1;
}

int InnotronicDeltaArm::channel_for_right() const {
    return this->is_motors_swapped() ? 1 : 2;
}

uint8_t InnotronicDeltaArm::select_for_left() const {
    return this->is_motors_swapped() ? 0x20 : 0x10;
}

uint8_t InnotronicDeltaArm::select_for_right() const {
    return this->is_motors_swapped() ? 0x10 : 0x20;
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
    if (this->is_motors_swapped()) {
        this->motor->send_delta_angle_cmd(0x30, right_ticks, speed_right, left_ticks, speed_left);
    } else {
        this->motor->send_delta_angle_cmd(0x30, left_ticks, speed_left, right_ticks, speed_right);
    }
    this->target_left_deg = left_deg;
    this->target_right_deg = right_deg;
    this->is_settling = false;
    this->stable_since = 0;
    this->was_stalling = false;
    this->stall_since = 0;
    this->properties.at("stalled")->boolean_value = false;
    this->properties.at("state")->integer_value = 1;
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
    this->last_ref_left = 0;
    this->last_ref_right = 0;
    this->brake_sent_left = false;
    this->brake_sent_right = false;
    this->cal_started_at = millis();
    this->properties.at("state")->integer_value = 0;
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
    // CW finds the left endstop, CCW finds the right endstop — these directions are bound to the
    // physical mount, not to a channel number. Only the channel mapping flips when motors_swapped.
    if (side == "left") {
        this->cal_state = cal_left;
        this->motor->reference_drive_start(this->channel_for_left(), true);
        echo("%s: reference left started", this->name.c_str());
    } else if (side == "right") {
        this->cal_state = cal_right;
        this->motor->reference_drive_start(this->channel_for_right(), false);
        echo("%s: reference right started", this->name.c_str());
    } else {
        // Hotfix: warmup-both lifts both arms; its REF_OK is not trusted (first reference
        // after power-on leaves motor left off-position). After the warmup we re-reference
        // left and right individually — only those singles update calibrated_*. Combined
        // frame: some firmware revs only honor "both" when m1 and m2 arrive in the same
        // 0x0C frame. 0x10=CW, 0x20=CCW.
        this->properties.at("calibrated_left")->boolean_value = false;
        this->properties.at("calibrated_right")->boolean_value = false;
        this->cal_state = cal_both;
        this->both_left_done = false;
        this->both_right_done = false;
        this->in_both_sequence = true;
        uint8_t cmd_m1 = this->is_motors_swapped() ? 0x20 : 0x10;
        uint8_t cmd_m2 = this->is_motors_swapped() ? 0x10 : 0x20;
        this->motor->send_single_motor_control(cmd_m1, cmd_m2);
        echo("%s: reference both (warmup) started", this->name.c_str());
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
    // motors_swapped routes the physical-side reading to the correct logical side.
    double cur_m1 = this->motor->get_property("angle_m1")->number_value;
    double cur_m2 = this->motor->get_property("angle_m2")->number_value;
    double cur_left_ticks = this->is_motors_swapped() ? cur_m2 : cur_m1;
    double cur_right_ticks = this->is_motors_swapped() ? cur_m1 : cur_m2;
    double cur_l_deg = cur_left_ticks * this->deg_per_tick;
    double cur_r_deg = cur_right_ticks * this->deg_per_tick;
    this->properties.at("angle_left")->number_value = cur_l_deg;
    this->properties.at("angle_right")->number_value = cur_r_deg;

    // Move-state tracking (no velocity feedback in delta mode).
    // state: 0=idle, 1=received (ack), 2=moving.
    // The first step after move_to defers any transition by leveraging is_settling=false
    // (reset in move_to). This guarantees state=1 is broadcast at least once.
    int64_t state = this->properties.at("state")->integer_value;
    if (state >= 1) {
        double tolerance_window_deg = this->deg_per_tick * POSITION_TOL_FACTOR;
        bool in_tol = std::abs(cur_l_deg - this->target_left_deg) <= tolerance_window_deg &&
                      std::abs(cur_r_deg - this->target_right_deg) <= tolerance_window_deg;
        if (state == 1 && !this->is_settling) {
            // First step after move_to: ensure state=1 broadcasts before any transition.
            this->stable_since = millis();
            this->is_settling = true;
        } else if (in_tol) {
            if (!this->is_settling) {
                this->stable_since = millis();
                this->is_settling = true;
            } else if (millis_since(this->stable_since) >= STABLE_MS) {
                this->properties.at("state")->integer_value = 0;
                this->is_settling = false;
            }
        } else {
            if (state == 1) {
                this->properties.at("state")->integer_value = 2;
            }
            this->is_settling = false;
        }
    }

    // Endstop safety is handled at move start by can_move() (blocks moves that would
    // drive past an already-active endstop) and physically by the motor firmware itself.
    // No runtime brake here — it would interfere with normal home parking at (0,0).
    this->left_endstop_prev = left_endstop_active;
    this->right_endstop_prev = right_endstop_active;

    // Stall guard: overcurrent AND position not moving.
    // High current while moving = legitimate load; high current with no progress = real stall.
    if (this->properties.at("state")->integer_value >= 1 && this->cal_state == cal_idle) {
        const double i_max = this->properties.at("stall_current")->number_value;
        const double i_m1 = this->motor->get_property("current_m1")->number_value;
        const double i_m2 = this->motor->get_property("current_m2")->number_value;
        const bool overcurrent = std::abs(i_m1) > i_max || std::abs(i_m2) > i_max;
        if (overcurrent) {
            if (!this->was_stalling) {
                this->stall_since = millis();
                this->stall_start_l_deg = cur_l_deg;
                this->stall_start_r_deg = cur_r_deg;
                this->was_stalling = true;
            } else if (std::abs(cur_l_deg - this->stall_start_l_deg) > STALL_POS_TOL_DEG ||
                       std::abs(cur_r_deg - this->stall_start_r_deg) > STALL_POS_TOL_DEG) {
                // Motor still moving despite overcurrent — restart the window.
                this->stall_since = millis();
                this->stall_start_l_deg = cur_l_deg;
                this->stall_start_r_deg = cur_r_deg;
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
    // Route physical-channel result to logical side based on motors_swapped.
    int ref_m1 = this->motor->get_property("ref_result_m1")->integer_value;
    int ref_m2 = this->motor->get_property("ref_result_m2")->integer_value;
    int ref_left = this->is_motors_swapped() ? ref_m2 : ref_m1;
    int ref_right = this->is_motors_swapped() ? ref_m1 : ref_m2;
    if (ref_left != 0) {
        this->last_ref_left = ref_left;
    }
    if (ref_right != 0) {
        this->last_ref_right = ref_right;
    }

    // Calibration timeout: abort and disable. Higher-level systems have no way to observe
    // the timeout otherwise (calibrating just flips back to false as on success); disabling
    // surfaces it as enabled=false. Same shape as the stall path.
    if (this->cal_state != cal_idle) {
        const double timeout_s = this->properties.at("cal_timeout")->number_value;
        if (timeout_s > 0 && millis_since(this->cal_started_at) > static_cast<unsigned long>(timeout_s * 1000)) {
            this->properties.at("calibrating")->boolean_value = false;
            this->properties.at("calibrated_left")->boolean_value = false;
            this->properties.at("calibrated_right")->boolean_value = false;
            this->cal_state = cal_idle;
            this->in_both_sequence = false;
            echo("%s: calibration timeout after %.1fs — disabling", this->name.c_str(), timeout_s);
            this->disable();
        }
    }

    // Calibration state machine — endstop only triggers the brake; "done" is driven
    // by the motor's own 0x14 ReferenceFeedback (last_ref_mX becoming non-zero).
    switch (this->cal_state) {
    case cal_left:
        if (this->last_ref_left != 0) {
            if (this->last_ref_left == REF_OK) {
                this->properties.at("calibrated_left")->boolean_value = true;
                echo("%s: left endstop reached, calibration complete", this->name.c_str());
            } else {
                echo("%s: left reference failed (ref_result=%d)", this->name.c_str(), this->last_ref_left);
            }
            if (this->in_both_sequence && this->last_ref_left == REF_OK) {
                // Hotfix step 3: single-left succeeded, now run single-right to finish the chain.
                this->cal_state = cal_idle;
                this->start_reference("right");
                if (this->cal_state == cal_idle) {
                    this->in_both_sequence = false;
                    this->properties.at("calibrating")->boolean_value = false;
                }
            } else {
                this->in_both_sequence = false;
                this->properties.at("calibrating")->boolean_value = false;
                this->cal_state = cal_idle;
            }
        } else if (left_endstop_active && !this->brake_sent_left) {
            this->motor->reference_drive_stop(this->channel_for_left());
            this->brake_sent_left = true;
            echo("%s: left endstop reached, waiting for motor confirmation", this->name.c_str());
        }
        break;
    case cal_right:
        if (this->last_ref_right != 0) {
            if (this->last_ref_right == REF_OK) {
                this->properties.at("calibrated_right")->boolean_value = true;
                echo("%s: right endstop reached, calibration complete", this->name.c_str());
            } else {
                echo("%s: right reference failed (ref_result=%d)", this->name.c_str(), this->last_ref_right);
            }
            this->in_both_sequence = false;
            this->properties.at("calibrating")->boolean_value = false;
            this->cal_state = cal_idle;
        } else if (right_endstop_active && !this->brake_sent_right) {
            this->motor->reference_drive_stop(this->channel_for_right());
            this->brake_sent_right = true;
            echo("%s: right endstop reached, waiting for motor confirmation", this->name.c_str());
        }
        break;
    case cal_both:
        if (!this->both_left_done) {
            if (this->last_ref_left != 0) {
                echo("%s: left endstop reached during both-reference (ref_result=%d)",
                     this->name.c_str(), this->last_ref_left);
                this->both_left_done = true;
            } else if (left_endstop_active && !this->brake_sent_left) {
                this->motor->reference_drive_stop(this->channel_for_left());
                this->brake_sent_left = true;
            }
        }
        if (!this->both_right_done) {
            if (this->last_ref_right != 0) {
                echo("%s: right endstop reached during both-reference (ref_result=%d)",
                     this->name.c_str(), this->last_ref_right);
                this->both_right_done = true;
            } else if (right_endstop_active && !this->brake_sent_right) {
                this->motor->reference_drive_stop(this->channel_for_right());
                this->brake_sent_right = true;
            }
        }
        if (this->both_left_done && this->both_right_done) {
            const bool warmup_ok = this->last_ref_left == REF_OK && this->last_ref_right == REF_OK;
            echo("%s: warmup both done (left=%d right=%d)",
                 this->name.c_str(), this->last_ref_left, this->last_ref_right);
            if (this->in_both_sequence && warmup_ok) {
                // Hotfix step 2: warmup acknowledged, now run the trustworthy single-left.
                // start_reference handles the endstop backoff (arm is sitting at the endstop
                // after the warmup) and resets the per-phase ref tracking state.
                this->cal_state = cal_idle;
                this->start_reference("left");
                if (this->cal_state == cal_idle) {
                    // start_reference rejected (e.g. motor got disabled mid-sequence).
                    this->in_both_sequence = false;
                    this->properties.at("calibrating")->boolean_value = false;
                }
            } else {
                this->in_both_sequence = false;
                this->properties.at("calibrating")->boolean_value = false;
                this->cal_state = cal_idle;
            }
        }
        break;
    case cal_backoff: {
        // Back off in the operating-range direction (left mount: negative ticks, right mount: positive).
        // The (channel ↔ side) mapping flips on motors_swapped; the per-mount tick sign stays.
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
            int16_t a_left = static_cast<int16_t>(cur_left_ticks);
            int16_t a_right = static_cast<int16_t>(cur_right_ticks);
            if (pick_left) {
                int16_t target = a_left - BACKOFF_STEP_TICKS;
                echo("%s: backoff left angle=%d -> %d", this->name.c_str(), a_left, target);
                this->motor->send_delta_angle_cmd(this->select_for_left(), target, BACKOFF_SPEED);
                this->backoff_last_was_left = true;
            } else if (nudge_right) {
                int16_t target = a_right + BACKOFF_STEP_TICKS;
                echo("%s: backoff right angle=%d -> %d", this->name.c_str(), a_right, target);
                this->motor->send_delta_angle_cmd(this->select_for_right(), target, BACKOFF_SPEED);
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
                this->in_both_sequence = false;
                this->properties.at("calibrating")->boolean_value = false;
                echo("%s: calibration aborted", this->name.c_str());
            }
            this->motor->stop();
            this->properties.at("state")->integer_value = 0;
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
    this->properties.at("state")->integer_value = 0;
}
