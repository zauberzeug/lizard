#include "uu_motor.h"
#include <cstring>
#include <memory>

REGISTER_MODULE_DEFAULTS(UUMotor)

const std::map<std::string, Variable_ptr> UUMotor::get_defaults() {
    return {};
}

UUMotor::UUMotor(const std::string &name, const Can_ptr can, const uint32_t can_id, uu_registers::MotorType type)
    : Module(uu_motor, name),
      can(can),
      can_id(can_id),
      motor_type(type),
      registers(uu_registers::REGISTER_MAP.at(motor_type).first),
      register_number(uu_registers::REGISTER_MAP.at(motor_type).second),
      last_can_msg_time(esp_timer_get_time()) {
}

void UUMotor::setup_pdo_motor1() {

    uint32_t control_mode1_value = (uu_registers::MOTOR1_REGISTERS.CONTROL_MODE << 16) | uu_registers::PDO_DELAY;
    this->can_write(0x3200, uu_registers::DLC_U32, control_mode1_value);
    uint32_t speed1_value = (uu_registers::MOTOR1_REGISTERS.MOTOR_SPEED_RPM << 16) | uu_registers::PDO_DELAY;
    this->can_write(0x3204, uu_registers::DLC_U32, speed1_value);
    uint32_t error_code1_value = (uu_registers::MOTOR1_REGISTERS.ERROR_CODE << 16) | uu_registers::PDO_DELAY;
    this->can_write(0x3208, uu_registers::DLC_U32, error_code1_value);
    uint32_t motor_running_status1_value = (uu_registers::MOTOR1_REGISTERS.MOTOR_RUNNING_STATUS << 16) | uu_registers::PDO_DELAY;
    this->can_write(0x320C, uu_registers::DLC_U32, motor_running_status1_value);
    uint32_t current1_value = (uu_registers::MOTOR1_REGISTERS.CURRENT << 16) | uu_registers::PDO_DELAY;
    this->can_write(0x3210, uu_registers::DLC_U32, current1_value);
}

void UUMotor::setup_pdo_motor2() {
    uint32_t control_mode2_value = (uu_registers::MOTOR2_REGISTERS.CONTROL_MODE << 16) | uu_registers::PDO_DELAY;
    this->can_write(0x3202, uu_registers::DLC_U32, control_mode2_value);
    uint32_t speed2_value = (uu_registers::MOTOR2_REGISTERS.MOTOR_SPEED_RPM << 16) | uu_registers::PDO_DELAY;
    this->can_write(0x3206, uu_registers::DLC_U32, speed2_value);
    uint32_t error_code2_value = (uu_registers::MOTOR2_REGISTERS.ERROR_CODE << 16) | uu_registers::PDO_DELAY;
    this->can_write(0x320A, uu_registers::DLC_U32, error_code2_value);
    uint32_t motor_running_status2_value = (uu_registers::MOTOR2_REGISTERS.MOTOR_RUNNING_STATUS << 16) | uu_registers::PDO_DELAY;
    this->can_write(0x320E, uu_registers::DLC_U32, motor_running_status2_value);
    uint32_t current2_value = (uu_registers::MOTOR2_REGISTERS.CURRENT << 16) | uu_registers::PDO_DELAY;
    this->can_write(0x3212, uu_registers::DLC_U32, current2_value);
}

void UUMotor::can_read(const uint16_t index) {
    uint32_t can_msg_id = 0;

    can_msg_id |= (0 << 28);                     // Scope/Direction (0 = Request)
    can_msg_id |= (this->can_id << 20);          // Slave ID
    can_msg_id |= (this->register_number << 16); // Register Number (1 = Single motor, 2 = Combined)
    can_msg_id |= (index & 0xFFFF);              // Register Address

    uint8_t data[8] = {0};

    this->can->send(can_msg_id, data, true, 0, true);
}

void UUMotor::can_write(const uint16_t index, const uint8_t dlc, const uint32_t value, const bool wait) {
    uint32_t can_msg_id = 0;

    can_msg_id |= (0 << 28);                     // Scope/Direction (0 = Request)
    can_msg_id |= (this->can_id << 20);          // Slave ID
    can_msg_id |= (this->register_number << 16); // Register Number (1 = Single motor, 2 = Combined)
    can_msg_id |= (index & 0xFFFF);              // Register Address

    uint8_t data[8] = {0};
    if (dlc == 1) {
        data[0] = value & 0xFF;
    } else if (dlc == 2) {
        data[0] = (value >> 8) & 0xFF; // MSB first
        data[1] = (value >> 0) & 0xFF; // LSB last
    } else if (dlc == 4) {
        data[0] = (value >> 24) & 0xFF; // MSB first
        data[1] = (value >> 16) & 0xFF;
        data[2] = (value >> 8) & 0xFF;
        data[3] = (value >> 0) & 0xFF; // LSB last
    }
    this->can->send(can_msg_id, data, false, dlc, true);
}

void UUMotor::subscribe_to_can() {
    // this is a pure virtual function
}

void UUMotor::reset_motor_error() {
    // this is a pure virtual function
}

void UUMotor::off() {
    // this is a pure virtual function
}

void UUMotor::set_speed(const double speed) {
    // this is a pure virtual function
}

void UUMotor::reset_estop() {
    // this is a pure virtual function
}

void UUMotor::step() {
    int64_t current_time = esp_timer_get_time();

    // If more than 3 seconds have passed since the last CAN message
    if (current_time - last_can_msg_time > uu_registers::CAN_WATCHDOG_TIME) {
        this->properties.at("error_flag")->boolean_value = true;
        this->off();
        last_can_msg_time = current_time;
    }
}
