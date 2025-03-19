#include "uu_motor.h"
#include <cstring>
#include <memory>

REGISTER_MODULE_DEFAULTS(UUMotor_combined)

const std::map<std::string, Variable_ptr> UUMotor_combined::get_defaults() {
    return {
        {"control_mode1", std::make_shared<IntegerVariable>()},
        {"control_mode2", std::make_shared<IntegerVariable>()},
        {"speed", std::make_shared<NumberVariable>()},
        {"speed1", std::make_shared<NumberVariable>()},
        {"speed2", std::make_shared<NumberVariable>()},
        {"error_code1", std::make_shared<IntegerVariable>()},
        {"error_code2", std::make_shared<IntegerVariable>()},
        {"motor_running_status1", std::make_shared<IntegerVariable>()},
        {"motor_running_status2", std::make_shared<IntegerVariable>()},
        {"error_flag", std::make_shared<BooleanVariable>()},
        {"m_per_tick", std::make_shared<NumberVariable>(1.0)},
        {"reversed", std::make_shared<BooleanVariable>()},
    };
}
UUMotor_combined::UUMotor_combined(const std::string &name, const Can_ptr can, const uint32_t can_id, uu_registers::MotorType type)
    : UUMotor(name, can, can_id, uu_registers::MotorType::COMBINED) {
    this->properties = UUMotor_combined::get_defaults();

    this->setup_pdo_motor1();
    this->setup_pdo_motor2();
}
void UUMotor_combined::subscribe_to_can() {
    const uint16_t register_addresses[] = {
        this->registers.CONTROL_MODE,
        this->registers.MOTOR_RUNNING_STATUS,
        this->registers.MOTOR_SPEED_RPM,
        this->registers.ERROR_CODE};

    // Loop through all registers and subscribe
    for (const auto &reg_addr : register_addresses) {
        uint32_t can_msg_id = 0;
        can_msg_id |= (1 << 28);            // Scope/Direction
        can_msg_id |= (this->can_id << 20); // Slave ID
        can_msg_id |= (1 << 16);            // Register Number
        can_msg_id |= (reg_addr & 0xFFFF);  // Register Address

        this->can->subscribe(can_msg_id, std::static_pointer_cast<Module>(this->shared_from_this()));

        // PDOs for the second motor in combined mode
        if (this->motor_type == uu_registers::MotorType::COMBINED) {
            if (reg_addr == this->registers.ERROR_CODE) { // error code step is 2 bytes
                can_msg_id |= ((reg_addr + 2) & 0xFFFF);
            } else {
                can_msg_id |= ((reg_addr + 1) & 0xFFFF);
            }
            this->can->subscribe(can_msg_id, std::static_pointer_cast<Module>(this->shared_from_this()));
        }
    }
}

void UUMotor_combined::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {

    if (method_name == "speed") {
        Module::expect(arguments, 1, numbery);
        this->set_speed(arguments[0]->evaluate_number());
    } else if (method_name == "off") {
        Module::expect(arguments, 0);
        this->off();
    } else if (method_name == "start") {
        Module::expect(arguments, 0);
        this->start();
    } else if (method_name == "reset_motor") {
        Module::expect(arguments, 0);
        this->reset_motor_error();
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->stop();
    } else if (method_name == "read") {
        Module::expect(arguments, 1, integer);
        this->can_read(arguments[0]->evaluate_integer());
    } else if (method_name == "write") {
        Module::expect(arguments, 3, integer, integer, integer);
        this->can_write(arguments[0]->evaluate_integer(), arguments[1]->evaluate_integer(), arguments[2]->evaluate_integer());
    } else if (method_name == "setup_motor") {
        Module::expect(arguments, 0);
        this->setup_motor();
    } else {
        Module::call(method_name, arguments);
    }
}

void UUMotor_combined::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    uint16_t reg_addr = id & 0xFFFF; // Extract register address from CAN ID
    this->last_can_msg_time = esp_timer_get_time();

    const auto &reg = this->registers;
    if (reg_addr == reg.MOTOR_SPEED_RPM) {
        uint16_t speed1 = data[1] | (data[0] << 8);
        this->properties.at("speed1")->number_value = (speed1 * this->properties.at("m_per_tick")->number_value *
                                                       (this->properties.at("reversed")->boolean_value ? -1 : 1)) /
                                                      60 / 10; // the uu_motor gives the speed in turn/m with the format xxx.x but we read xxxx for this reason we devide by 10 to move the decimal point
    } else if (reg_addr == reg.MOTOR_SPEED_RPM + 1) {
        uint16_t speed2 = data[1] | (data[0] << 8);
        this->properties.at("speed2")->number_value = (speed2 * this->properties.at("m_per_tick")->number_value *
                                                       (this->properties.at("reversed")->boolean_value ? -1 : 1)) /
                                                      60 / 10; // the uu_motor gives the speed in turn/m with the format xxx.x but we read xxxx for this reason we devide by 10 to move the decimal point
        this->properties.at("speed")->number_value = (this->properties.at("speed1")->number_value + this->properties.at("speed2")->number_value) / 2;
    } else if (reg_addr == reg.ERROR_CODE) {
        uint32_t error_code1 = (uint32_t)data[0] << 24 |
                               (uint32_t)data[1] << 16 |
                               (uint32_t)data[2] << 8 |
                               (uint32_t)data[3];
        this->properties.at("error_code1")->integer_value = error_code1;
        if (error_code1 != 0) {
            this->properties.at("error_flag")->boolean_value = true;
        }
    } else if (reg_addr == reg.ERROR_CODE + 2) {
        uint32_t error_code2 = (uint32_t)data[0] << 24 |
                               (uint32_t)data[1] << 16 |
                               (uint32_t)data[2] << 8 |
                               (uint32_t)data[3];
        this->properties.at("error_code2")->integer_value = error_code2;
        if (error_code2 != 0) {
            this->properties.at("error_flag")->boolean_value = true;
        }
    } else if (reg_addr == reg.MOTOR_RUNNING_STATUS) {
        uint16_t motor_running_status1 = data[1] | (data[0] << 8);
        this->properties.at("motor_running_status1")->integer_value = motor_running_status1;
    } else if (reg_addr == reg.MOTOR_RUNNING_STATUS + 1) {
        uint16_t motor_running_status2 = data[1] | (data[0] << 8);
        this->properties.at("motor_running_status2")->integer_value = motor_running_status2;
    } else if (reg_addr == reg.CONTROL_MODE) {
        uint16_t control_mode1 = data[1] | (data[0] << 8);
        this->properties.at("control_mode1")->integer_value = control_mode1;
    } else if (reg_addr == reg.CONTROL_MODE + 1) {
        uint16_t control_mode2 = data[1] | (data[0] << 8);
        this->properties.at("control_mode2")->integer_value = control_mode2;
    }
}

void UUMotor_combined::can_write_combined(const uint16_t reg_addr, const uint16_t value) {
    uint32_t combined_value = ((uint32_t)value << 16) | value;
    this->can_write(reg_addr, 4, combined_value);
}

void UUMotor_combined::set_speed(const double speed) {
    if (this->properties.at("error_flag")->boolean_value and this->properties.at("error_code1")->integer_value == 0 and this->properties.at("error_code2")->integer_value == 0) {
        this->reset_motor_error();
    }

    if (this->properties.at("motor_running_status1")->integer_value != uu_registers::MOTOR_RUNNING_STATUS_RUNNING ||
        this->properties.at("motor_running_status2")->integer_value != uu_registers::MOTOR_RUNNING_STATUS_RUNNING) {
        this->start();
    }
    // For combined mode, check both control modes
    if (this->properties.at("control_mode1")->integer_value != uu_registers::CONTROL_MODE_SPEED ||
        this->properties.at("control_mode2")->integer_value != uu_registers::CONTROL_MODE_SPEED) {
        this->set_mode(uu_registers::CONTROL_MODE_SPEED);
    }
    int actual_speed = (speed * 60 / this->properties.at("m_per_tick")->number_value /
                        (this->properties.at("reversed")->boolean_value ? -1 : 1));
    this->can_write_combined(this->registers.MOTOR_SET_SPEED, actual_speed);
}

void UUMotor_combined::set_mode(const uint16_t control_mode) {
    if (this->properties.at("control_mode1")->integer_value != control_mode ||
        this->properties.at("control_mode2")->integer_value != control_mode) {
        this->can_write_combined(this->registers.CONTROL_MODE, control_mode);
    }
}

void UUMotor_combined::reset_motor_error() {
    if (this->properties.at("error_flag")->boolean_value) {
        this->setup_pdo_motor1();
        this->setup_pdo_motor2();
        this->can_write_combined(this->registers.CONTROL_COMMAND, uu_registers::CLEAR_ERRORS);
        this->properties.at("error_flag")->boolean_value = false;
    }
}

void UUMotor_combined::setup_motor() {
    this->can_write_combined(this->registers.SET_HALL, uu_registers::SENSOR_TYPE);
    this->can_write_combined(this->registers.CALIBRATION, uu_registers::START_CALIBRATION);
}

void UUMotor_combined::off() {
    this->can_write_combined(this->registers.CONTROL_COMMAND, uu_registers::STOP_MOTOR);
}

void UUMotor_combined::start() {
    this->can_write_combined(this->registers.CONTROL_COMMAND, uu_registers::START_MOTOR);
}

void UUMotor_combined::stop() {
    this->set_speed(0);
}
