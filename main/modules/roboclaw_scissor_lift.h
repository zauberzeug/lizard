#pragma once

#include "roboclaw_motor.h"

class RoboClawScissorLift : public Module {
private:
    static constexpr double MAX_SYNC_DIFF = 10.0; // percent

    // State machine
    // IDLE:     kein aktiver Fahrbefehl, Sync-Check inaktiv
    // MOVING:   drive() aktiv, Sync-Check läuft
    // FAULTED:  Notstopp ausgelöst, kein neues drive() erlaubt
    // RELAXING: relax() läuft, Sync-Check inaktiv
    enum class State { IDLE, MOVING, FAULTED, RELAXING };
    State state = State::IDLE;

    // Gespeicherte Parameter aus letztem drive()-Aufruf
    double target_percent = 0.0;
    uint32_t active_speed = 0;   // 0 = noch kein drive() aufgerufen → Fallback auf Property
    uint32_t active_accel = 0;
    uint32_t active_deccel = 0;

    // Letzter abgerundeter correction_diff-Wert.
    // Verhindert redundante Positionsbefehle wenn sich die Stufe nicht ändert.
    int last_diff_step = -1;

    // Welcher Motor hat beim Notstopp vorgeeiilt? (1 oder 2, 0 = unbekannt)
    // Wird von relax() genutzt.
    int leading_motor = 0;

    const RoboClawMotor_ptr motor1;
    const RoboClawMotor_ptr motor2;
    bool enabled = true;

    void enable();
    void disable();
    void drive(double percent, uint32_t speed, uint32_t accel, uint32_t deccel);
    void stop();
    void relax();

public:
    RoboClawScissorLift(const std::string name, const RoboClawMotor_ptr motor1, const RoboClawMotor_ptr motor2);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
