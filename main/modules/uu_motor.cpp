#include "uu_motor.h"
#include <cstring>
#include <memory>

UUMotor::UUMotor(const std::string &name, const Can_ptr can, const uint32_t can_id, uu_registers::MotorType type)
    : Module(uu_motor, name),
      can(can),
      can_id(can_id),
      motor_type(type),
      registers(uu_registers::REGISTER_MAP.at(motor_type).first),
      register_number(uu_registers::REGISTER_MAP.at(motor_type).second),
      last_can_msg_time(esp_timer_get_time()) {
    if (motor_type == uu_registers::MotorType::COMBINED) {
        this->properties["control_mode1"] = std::make_shared<IntegerVariable>();
        this->properties["control_mode2"] = std::make_shared<IntegerVariable>();
        this->properties["speed1"] = std::make_shared<IntegerVariable>();
        this->properties["speed2"] = std::make_shared<IntegerVariable>();
        this->properties["error_code1"] = std::make_shared<IntegerVariable>();
        this->properties["error_code2"] = std::make_shared<IntegerVariable>();
        this->properties["motor_running_status1"] = std::make_shared<IntegerVariable>();
        this->properties["motor_running_status2"] = std::make_shared<IntegerVariable>();
        this->properties["error_flag"] = std::make_shared<BooleanVariable>();

    } else {
        this->properties["control_mode"] = std::make_shared<IntegerVariable>();
        this->properties["error_code"] = std::make_shared<IntegerVariable>();
        this->properties["motor_running_status"] = std::make_shared<IntegerVariable>();
        this->properties["speed"] = std::make_shared<IntegerVariable>();
        this->properties["error_flag"] = std::make_shared<BooleanVariable>();
    }

    // Set the PDOs for the motor
    if (motor_type == uu_registers::MotorType::COMBINED) {
        this->setup_pdo_motor1();
        this->setup_pdo_motor2();
    } else if (motor_type == uu_registers::MotorType::MOTOR1) {
        this->setup_pdo_motor1();
    } else if (motor_type == uu_registers::MotorType::MOTOR2) {
        this->setup_pdo_motor2();
    }
}

void UUMotor::setup_pdo_motor1() {

    uint32_t control_mode1_value = (uu_registers::MOTOR1_REGISTERS.CONTROL_MODE << 16) | 100;
    this->can_write(0x3200, uu_registers::DLC_U32, control_mode1_value);
    uint32_t speed1_value = (uu_registers::MOTOR1_REGISTERS.MOTOR_SPEED_RPM << 16) | 100;
    this->can_write(0x3204, uu_registers::DLC_U32, speed1_value);
    uint32_t error_code1_value = (uu_registers::MOTOR1_REGISTERS.ERROR_CODE << 16) | 100;
    this->can_write(0x3208, uu_registers::DLC_U32, error_code1_value);
    uint32_t motor_running_status1_value = (uu_registers::MOTOR1_REGISTERS.MOTOR_RUNNING_STATUS << 16) | 100;
    this->can_write(0x320C, uu_registers::DLC_U32, motor_running_status1_value);
}

void UUMotor::setup_pdo_motor2() {
    uint32_t control_mode2_value = (uu_registers::MOTOR2_REGISTERS.CONTROL_MODE << 16) | 100;
    this->can_write(0x3202, uu_registers::DLC_U32, control_mode2_value);
    uint32_t speed2_value = (uu_registers::MOTOR2_REGISTERS.MOTOR_SPEED_RPM << 16) | 100;
    this->can_write(0x3206, uu_registers::DLC_U32, speed2_value);
    uint32_t error_code2_value = (uu_registers::MOTOR2_REGISTERS.ERROR_CODE << 16) | 100;
    this->can_write(0x320A, uu_registers::DLC_U32, error_code2_value);
    uint32_t motor_running_status2_value = (uu_registers::MOTOR2_REGISTERS.MOTOR_RUNNING_STATUS << 16) | 100;
    this->can_write(0x320E, uu_registers::DLC_U32, motor_running_status2_value);
}

// TODO: this needs to be tested
void UUMotor::subscribe_to_can() {
    const uint16_t register_addresses[] = {
        this->registers.CONTROL_MODE,
        this->registers.MOTOR_RUNNING_STATUS,
        this->registers.MOTOR_SPEED_RPM,
        this->registers.ERROR_CODE};

    // Durch alle Register loopen und subscriben
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

void UUMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    last_can_msg_time = esp_timer_get_time();

    // Extrahiere Informationen aus der CAN ID
    uint8_t direction = (id >> 28) & 0xF; // Sollte 1 sein
    uint8_t slave_id = (id >> 20) & 0xFF;
    uint8_t reg_num = (id >> 16) & 0xF;
    uint16_t reg_addr = id & 0xFFFF;

    // Verarbeite die Nachricht basierend auf dem Motortyp
    if (this->motor_type == uu_registers::MotorType::COMBINED) {
        this->handle_combined_can_msg(reg_addr, data);
    } else {
        this->handle_single_can_msg(reg_addr, data);
    }
}
void UUMotor::handle_combined_can_msg(const uint16_t reg_addr, const uint8_t *const data) {
    const auto &reg = this->registers;
    if (reg_addr == reg.MOTOR_SPEED_RPM) {
        uint16_t speed1 = data[1] | (data[0] << 8);
        this->properties.at("speed1")->integer_value = speed1;
    } else if (reg_addr == reg.MOTOR_SPEED_RPM + 1) {
        uint16_t speed2 = data[1] | (data[0] << 8);
        this->properties.at("speed2")->integer_value = speed2;
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

void UUMotor::handle_single_can_msg(const uint16_t reg_addr, const uint8_t *const data) {
    const auto &reg = this->registers;
    if (reg_addr == reg.MOTOR_RUNNING_STATUS) {
        uint16_t motor_running_status = data[1] | (data[0] << 8);
        this->properties.at("motor_running_status")->integer_value = motor_running_status;
    } else if (reg_addr == reg.MOTOR_SPEED_RPM) {
        uint16_t speed = data[1] | (data[0] << 8);
        this->properties.at("speed")->integer_value = speed;
    } else if (reg_addr == reg.ERROR_CODE) {
        uint32_t error_code = (uint32_t)data[0] << 24 |
                              (uint32_t)data[1] << 16 |
                              (uint32_t)data[2] << 8 |
                              (uint32_t)data[3];
        this->properties.at("error_code")->integer_value = error_code;
        if (error_code != 0) {
            this->properties.at("error_flag")->boolean_value = true;
        }
    } else if (reg_addr == reg.CONTROL_MODE) {
        uint16_t control_mode = data[1] | (data[0] << 8);
        this->properties.at("control_mode")->integer_value = control_mode;
    }
}

// read a single register
void UUMotor::can_read(const uint16_t index) {
    uint32_t can_msg_id = 0;

    // Aufbau der CAN ID nach der Struktur
    can_msg_id |= (0 << 28);                     // Scope/Direction (0 = Request)
    can_msg_id |= (this->can_id << 20);          // Slave ID
    can_msg_id |= (this->register_number << 16); // Register Number (1 = Single motor, 2 = Combined)
    can_msg_id |= (index & 0xFFFF);              // Register Address

    uint8_t data[8] = {0};

    this->can->send(can_msg_id, data, true, 0, true);
}

// write a single register
void UUMotor::can_write(const uint16_t index, const uint8_t dlc, const uint32_t value, const bool wait) {
    uint32_t can_msg_id = 0;

    // Aufbau der CAN ID nach der Struktur
    can_msg_id |= (0 << 28);                     // Scope/Direction (0 = Request)
    can_msg_id |= (this->can_id << 20);          // Slave ID
    can_msg_id |= (this->register_number << 16); // Register Number (1 = Single motor, 2 = Combined)
    can_msg_id |= (index & 0xFFFF);              // Register Address
    ESP_LOGI("UUMotor", "The message is %lx", can_msg_id);

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

    char data_str[50] = {0};
    int pos = 0;
    for (int i = 0; i < dlc; i++) { // Vorwärts durch die Bytes iterieren
        pos += sprintf(data_str + pos, "%02x ", data[i]);
    }

    this->can->send(can_msg_id, data, false, dlc, true);
}

void UUMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {

    if (method_name == "speed") {
        Module::expect(arguments, 1, numbery);
        this->speed(arguments[0]->evaluate_number());
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

void UUMotor::speed(const int16_t speed) {
    ESP_LOGI("UUMotor", "Setting speed to %d", speed);
    if (motor_type == uu_registers::MotorType::COMBINED) {
        if (this->properties.at("motor_running_status1")->integer_value != uu_registers::MOTOR_RUNNING_STATUS_RUNNING &&
            this->properties.at("motor_running_status2")->integer_value != uu_registers::MOTOR_RUNNING_STATUS_RUNNING) {
            this->start();
            this->can_write(this->registers.CONTROL_COMMAND + 1, uu_registers::DLC_U16, uu_registers::START_MOTOR);
        }
        // For combined mode, check both control modes
        if (this->properties.at("control_mode1")->integer_value != uu_registers::CONTROL_MODE_SPEED ||
            this->properties.at("control_mode2")->integer_value != uu_registers::CONTROL_MODE_SPEED) {
            this->set_mode(uu_registers::CONTROL_MODE_SPEED);
        }
    } else {
        if (this->properties.at("motor_running_status")->integer_value != uu_registers::MOTOR_RUNNING_STATUS_RUNNING) {
            this->start();
            ESP_LOGI("UUMotor", "Starting motor");
        }
        // Single motor mode
        if (this->properties.at("control_mode")->integer_value != uu_registers::CONTROL_MODE_SPEED) {
            this->set_mode(uu_registers::CONTROL_MODE_SPEED);
            ESP_LOGI("UUMotor", "Setting control mode to speed");
        }
    }
    ESP_LOGI("UUMotor", "Writing speed to motor");
    this->can_write(this->registers.MOTOR_SET_SPEED, uu_registers::DLC_U16, speed);
}

void UUMotor::set_mode(const uint16_t control_mode) {
    if (motor_type == uu_registers::MotorType::COMBINED) {
        // For combined mode, check both control modes
        if (this->properties.at("control_mode1")->integer_value == control_mode &&
            this->properties.at("control_mode2")->integer_value == control_mode) {
            return;
        }
    } else {
        // Single motor mode
        if (this->properties.at("control_mode")->integer_value == control_mode) {
            return;
        }
    }
    this->can_write(this->registers.CONTROL_MODE, uu_registers::DLC_U16, control_mode);
}

void UUMotor::reset_motor_error() {
    if (this->properties.at("error_flag")->boolean_value) {
        this->setup_pdo_motor1();
        this->setup_pdo_motor2();
        this->can_write(this->registers.CONTROL_COMMAND, uu_registers::DLC_U16, uu_registers::CLEAR_ERRORS);
        this->properties.at("error_flag")->boolean_value = false;
    }
}

void UUMotor::setup_motor() {
    this->can_write(this->registers.SET_HALL, uu_registers::DLC_U16, uu_registers::SENSOR_TYPE);
    this->can_write(this->registers.CALIBRATION, uu_registers::DLC_U16, uu_registers::START_CALIBRATION);
}

void UUMotor::off() {
    this->can_write(this->registers.CONTROL_COMMAND, uu_registers::DLC_U16, uu_registers::STOP_MOTOR);
}

void UUMotor::start() {
    this->can_write(this->registers.CONTROL_COMMAND, uu_registers::DLC_U16, uu_registers::START_MOTOR);
}

void UUMotor::stop() {
    this->speed(0);
}

void UUMotor::step() {
    int64_t current_time = esp_timer_get_time();

    // Wenn mehr als 1 Sekunde seit der letzten CAN-Nachricht vergangen ist
    if (current_time - last_can_msg_time > 3000000) { // 1000000 Mikrosekunden = 1 Sekunde
        this->properties.at("error_flag")->boolean_value = true;
        ESP_LOGW("UUMotor", "CAN Watchdog triggered! No messages received for 1 second. Resetting...");
        this->reset_motor_error();
        last_can_msg_time = current_time;
    }
}

double UUMotor::get_position() {
    // TODO: Implementiere Position-Abfrage
    return 0.0;
}

void UUMotor::position(const double position, const double speed, const double acceleration) {
    // TODO: Implementiere Position-Steuerung
}

double UUMotor::get_speed() {
    // TODO: Implementiere Geschwindigkeits-Abfrage
    return 0.0;
}

void UUMotor::speed(const double speed, const double acceleration) {
    // TODO: Implementiere Geschwindigkeits-Steuerung
}