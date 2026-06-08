#include "roboclaw_scissor_lift.h"
#include <cmath>
#include <cstdlib>
#include <memory>
#include <stdexcept>

REGISTER_MODULE_DEFAULTS(RoboClawScissorLift)

const std::map<std::string, Variable_ptr> RoboClawScissorLift::get_defaults() {
    return {
        {"position", std::make_shared<NumberVariable>()},      // Mittelwert beider Motoren in %
        {"position1", std::make_shared<NumberVariable>()},     // Motor1 in %
        {"position2", std::make_shared<NumberVariable>()},     // Motor2 in %
        {"position_diff", std::make_shared<NumberVariable>()}, // p1 - p2 in %
        {"fault", std::make_shared<BooleanVariable>(false)},   // true nach Notstopp
        {"enabled", std::make_shared<BooleanVariable>(true)},
        {"default_speed", std::make_shared<IntegerVariable>(5000)},
        {"default_accel", std::make_shared<IntegerVariable>(20000)},
        {"default_deccel", std::make_shared<IntegerVariable>(20000)},
    };
}

RoboClawScissorLift::RoboClawScissorLift(const std::string name, const RoboClawMotor_ptr motor1, const RoboClawMotor_ptr motor2)
    : Module(roboclaw_scissor_lift, name), motor1(motor1), motor2(motor2) {
    this->properties = RoboClawScissorLift::get_defaults();
}

// Gibt die aktuelle Position eines Motors als Prozentwert zurück (0–100%)
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

// Rechnet einen Prozentwert in Encoder-Ticks für einen Motor um
static int32_t percent_to_ticks(const RoboClawMotor_ptr &motor, double percent) {
    const int32_t low = motor->get_limit_low();
    const int32_t high = motor->get_limit_high();
    return static_cast<int32_t>(low + (percent / 100.0) * (high - low));
}

void RoboClawScissorLift::step() {
    if (this->motor1->is_calibrated() && this->motor2->is_calibrated()) {
        const double p1 = motor_percent(this->motor1);
        const double p2 = motor_percent(this->motor2);
        const double diff = p1 - p2;

        // Positionswerte aktualisieren
        this->properties.at("position")->number_value = (p1 + p2) / 2.0;
        this->properties.at("position1")->number_value = p1;
        this->properties.at("position2")->number_value = p2;
        this->properties.at("position_diff")->number_value = diff;

        if (this->state == State::MOVING) {
            // Verbleibende Strecke zum Ziel für jeden Motor (absolut, richtungsunabhängig)
            const double rem1 = std::abs(this->target_percent - p1);
            const double rem2 = std::abs(this->target_percent - p2);

            // correction_diff: wie viel weiter ist ein Motor vom Ziel entfernt als der andere?
            // Bei entgegengesetzten Bewegungen (Ziel liegt zwischen beiden) → 0 wenn gleich weit
            const double correction_diff = std::abs(rem1 - rem2);
            const int diff_step = static_cast<int>(std::floor(correction_diff));

            // Notstopp: ein Motor eilt dem anderen zu weit voraus
            if (correction_diff > MAX_SYNC_DIFF) {
                this->motor1->stop();
                this->motor2->stop();
                // Speichern welcher Motor vorgeeilt ist (weniger Restweg = nähер am Ziel)
                this->leading_motor = (rem1 < rem2) ? 1 : 2;
                this->state = State::FAULTED;
                this->properties.at("fault")->boolean_value = true;
                throw std::runtime_error("sync fault: correction_diff " +
                                         std::to_string(correction_diff) +
                                         "% — call lift.relax()");
            }

            // Option 2 (derzeit deaktiviert): Geschwindigkeitskorrektur
            // Nur neu senden wenn diff_step größer wird (schlechter), nicht beim Konvergieren.
            // Achtung: jedes Neusenden blockiert den Loop (~5ms pro Motor). Bei häufigen
            // Stufenwechseln kann das den Bus überlasten und Encoder-Reads verlangsamen.
            //
            // if (diff_step > this->last_diff_step) {
            //     const double factor = std::max(0.0, 1.0 - std::max(0, diff_step - 2) * 0.10);
            //     const bool motor1_leads = (rem1 < rem2);
            //     const uint32_t speed1 = motor1_leads
            //         ? static_cast<uint32_t>(this->active_speed * factor)
            //         : this->active_speed;
            //     const uint32_t speed2 = motor1_leads
            //         ? this->active_speed
            //         : static_cast<uint32_t>(this->active_speed * factor);
            //     this->motor1->position(percent_to_ticks(this->motor1, this->target_percent),
            //                            speed1, this->active_accel, this->active_deccel);
            //     this->motor2->position(percent_to_ticks(this->motor2, this->target_percent),
            //                            speed2, this->active_accel, this->active_deccel);
            //     this->last_diff_step = diff_step;
            // }

        } else if (this->state == State::RELAXING) {
            // Relax abgeschlossen wenn Motoren wieder synchron sind
            if (std::abs(diff) < 2.0) {
                this->target_percent = (p1 + p2) / 2.0;
                this->state = State::IDLE;
                this->properties.at("fault")->boolean_value = false;
            }
        }
    }

    // Enabled-Property mit internem Zustand abgleichen
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
                                   : static_cast<uint32_t>(this->properties.at("default_speed")->integer_value);
        const uint32_t accel = arguments.size() > 2
                                   ? static_cast<uint32_t>(std::abs((int32_t)arguments[2]->evaluate_number()))
                                   : static_cast<uint32_t>(this->properties.at("default_accel")->integer_value);
        const uint32_t deccel = arguments.size() > 3
                                    ? static_cast<uint32_t>(std::abs((int32_t)arguments[3]->evaluate_number()))
                                    : static_cast<uint32_t>(this->properties.at("default_deccel")->integer_value);
        this->drive(percent, speed, accel, deccel);
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->stop();
    } else if (method_name == "relax") {
        Module::expect(arguments, 0);
        this->relax();
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
        throw std::runtime_error("both motors must be calibrated with set_limits before driving");
    }

    // Check 1: kein Losfahren wenn Motoren zu weit auseinander sind.
    // relax() muss zuerst aufgerufen werden um den voreilenden Motor zurückzuholen.
    const double p1 = motor_percent(this->motor1);
    const double p2 = motor_percent(this->motor2);
    if (std::abs(p1 - p2) > MAX_SYNC_DIFF) {
        throw std::runtime_error("cannot drive: positions differ by " +
                                 std::to_string(std::abs(p1 - p2)) +
                                 "% — call lift.relax() first");
    }

    this->target_percent = percent;
    this->active_speed = speed;
    this->active_accel = accel;
    this->active_deccel = deccel;
    this->last_diff_step = -1; // erzwingt Neuberechnung in step()
    this->state = State::MOVING;
    this->properties.at("fault")->boolean_value = false;

    this->motor1->position(percent_to_ticks(this->motor1, percent), speed, accel, deccel);
    this->motor2->position(percent_to_ticks(this->motor2, percent), speed, accel, deccel);
}

void RoboClawScissorLift::stop() {
    this->motor1->stop();
    this->motor2->stop();
    // Zielposition auf aktuellen Mittelwert setzen.
    // Ermöglicht Sync-Korrektur auch im Stillstand ohne ungewollte Bewegung.
    if (this->motor1->is_calibrated() && this->motor2->is_calibrated()) {
        const double p1 = motor_percent(this->motor1);
        const double p2 = motor_percent(this->motor2);
        this->target_percent = (p1 + p2) / 2.0;
    }
    this->state = State::IDLE;
    this->last_diff_step = -1;
}

void RoboClawScissorLift::relax() {
    if (!this->motor1->is_calibrated() || !this->motor2->is_calibrated()) {
        throw std::runtime_error("motors not calibrated");
    }

    const double p1 = motor_percent(this->motor1);
    const double p2 = motor_percent(this->motor2);
    const double diff = p1 - p2;

    if (std::abs(diff) < 0.5) {
        // Bereits synchron, nichts zu tun
        this->state = State::IDLE;
        this->properties.at("fault")->boolean_value = false;
        return;
    }

    // Voreilenden Motor zur Position des nacheilenden schicken.
    // active_* enthält die Parameter des letzten drive()-Aufrufs.
    // Falls noch kein drive() lief (active_speed == 0) → Fallback auf Properties.
    const uint32_t speed = this->active_speed > 0
                               ? this->active_speed
                               : static_cast<uint32_t>(this->properties.at("default_speed")->integer_value);
    const uint32_t accel = this->active_accel > 0
                               ? this->active_accel
                               : static_cast<uint32_t>(this->properties.at("default_accel")->integer_value);
    const uint32_t deccel = this->active_deccel > 0
                                ? this->active_deccel
                                : static_cast<uint32_t>(this->properties.at("default_deccel")->integer_value);

    // Der 10%-Check in step() greift hier nicht (State::RELAXING).
    if (diff > 0) {
        // motor1 eilt vor → zurück auf motor2-Position
        this->motor1->position(percent_to_ticks(this->motor1, p2), speed, accel, deccel);
        this->leading_motor = 1;
    } else {
        // motor2 eilt vor → zurück auf motor1-Position
        this->motor2->position(percent_to_ticks(this->motor2, p1), speed, accel, deccel);
        this->leading_motor = 2;
    }

    this->state = State::RELAXING;
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
