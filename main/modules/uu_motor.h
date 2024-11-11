#pragma once

#include "can.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "module.h"
#include "motor.h"
#include <memory>

class UUMotor;
using UUMotor_ptr = std::shared_ptr<UUMotor>;
namespace uu_registers {

enum class MotorType {
    MOTOR1,  // Register von Motor 1, Register Number = 1
    MOTOR2,  // Register von Motor 2, Register Number = 1
    COMBINED // Register von Motor 1, Register Number = 2
};

struct MotorRegisters {
    uint16_t CONTROL_MODE;
    uint16_t CONTROL_COMMAND;
    uint16_t MOTOR_SET_SPEED;
    uint16_t MOTOR_SET_POSITION;
    uint16_t MOTOR_RUNNING_STATUS;
    uint16_t MOTOR_SPEED_RPM;
    uint16_t ERROR_CODE;
    uint16_t SET_HALL;
    uint16_t CALIBRATION;
};

constexpr MotorRegisters MOTOR1_REGISTERS = {
    .CONTROL_MODE = 0x5100,
    .CONTROL_COMMAND = 0x5300,
    .MOTOR_SET_SPEED = 0x5304,
    .MOTOR_SET_POSITION = 0x530C,
    .MOTOR_RUNNING_STATUS = 0x5400,
    .MOTOR_SPEED_RPM = 0x5410,
    .ERROR_CODE = 0x5420,
    .SET_HALL = 0x502C,
    .CALIBRATION = 0x5600};

// TODO: check if this is correct
constexpr MotorRegisters MOTOR2_REGISTERS = {
    .CONTROL_MODE = 0x5101,
    .CONTROL_COMMAND = 0x5301,
    .MOTOR_SET_SPEED = 0x5305,
    .MOTOR_SET_POSITION = 0x530D,
    .MOTOR_RUNNING_STATUS = 0x5401,
    .MOTOR_SPEED_RPM = 0x5411,
    .ERROR_CODE = 0x5422,
    .SET_HALL = 0x502D,
    .CALIBRATION = 0x5601};

// make register map
static const std::map<MotorType, std::pair<const MotorRegisters &, uint8_t>> REGISTER_MAP = {
    {MotorType::MOTOR1, {MOTOR1_REGISTERS, 1}},  // Motor 1 Register, Number = 1
    {MotorType::MOTOR2, {MOTOR2_REGISTERS, 1}},  // Motor 2 Register, Number = 1
    {MotorType::COMBINED, {MOTOR1_REGISTERS, 2}} // Motor 1 Register, Number = 2
};

// Control commands
constexpr uint8_t STOP_MOTOR = 0;
constexpr uint8_t START_MOTOR = 1;
constexpr uint8_t CLEAR_ERRORS = 2;

// Control modes
constexpr uint8_t CONTROL_MODE_SPEED = 0;
constexpr uint8_t CONTROL_MODE_POSITION = 1;

// Running status
constexpr uint8_t MOTOR_RUNNING_STATUS_RUNNING = 1;
constexpr uint8_t MOTOR_RUNNING_STATUS_STOPPED = 0;

// setup parameters
constexpr uint16_t SENSOR_TYPE = 1;
constexpr uint16_t START_CALIBRATION = 1;

// DLC
constexpr uint8_t DLC_NONE = 0;
constexpr uint8_t DLC_U8 = 1;
constexpr uint8_t DLC_U16 = 2;
constexpr uint8_t DLC_U32 = 4;
} // namespace uu_registers

class UUMotor : public Module, public std::enable_shared_from_this<UUMotor>, virtual public Motor {
private:
    const Can_ptr can;
    const uint32_t can_id;
    const uu_registers::MotorType motor_type;
    const uu_registers::MotorRegisters &registers;
    const uint8_t register_number;
    int64_t last_can_msg_time;

    void can_write(const uint16_t index, const uint8_t dlc, const uint32_t value, const bool wait = false);
    void can_read(const uint16_t index);
    void handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) override;
    void handle_single_can_msg(const uint16_t reg_addr, const uint8_t *const data);
    void handle_combined_can_msg(const uint16_t reg_addr, const uint8_t *const data);
    void setup_pdo_motor1();
    void setup_pdo_motor2();
    void set_mode(const uint16_t control_mode);
    void reset_motor_error();
    void speed(const int16_t speed);
    void setup_motor();
    void off();
    void start();
    void stop() override;

    // Implementierung der reinen virtuellen Funktionen von Motor
    double get_position() override;
    void position(const double position, const double speed, const double acceleration) override;
    double get_speed() override;
    void speed(const double speed, const double acceleration) override;

public:
    UUMotor(const std::string &name, const Can_ptr can, const uint32_t can_id, uu_registers::MotorType type = uu_registers::MotorType::MOTOR2);
    void subscribe_to_can();
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;

    void step() override;
};

class UUMotor_single : public UUMotor {
public:
    UUMotor_single(const std::string &name, const Can_ptr can, const uint32_t can_id, uu_registers::MotorType type = uu_registers::MotorType::MOTOR1);
};

class UUMotor_combined : public UUMotor {
public:
    UUMotor_combined(const std::string &name, const Can_ptr can, const uint32_t can_id);
};