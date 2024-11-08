#include "uu_motor.h"
#include <cstring>
#include <memory>

UUMotor::UUMotor(const std::string &name, const Can_ptr can, const uint32_t can_id, uu_registers::MotorType type)
    : Module(uu_motor, name),
      can(can),
      can_id(can_id),
      motor_type(uu_registers::MotorType::MOTOR2),
      registers(uu_registers::REGISTER_MAP.at(motor_type).first),
      register_number(uu_registers::REGISTER_MAP.at(motor_type).second) {
    if (motor_type == uu_registers::MotorType::COMBINED) {
        this->properties["control_mode1"] = std::make_shared<NumberVariable>();
        this->properties["control_mode2"] = std::make_shared<NumberVariable>();
        this->properties["speed1"] = std::make_shared<NumberVariable>();
        this->properties["speed2"] = std::make_shared<NumberVariable>();
        this->properties["error_code1"] = std::make_shared<NumberVariable>();
        this->properties["error_code2"] = std::make_shared<NumberVariable>();
        this->properties["motor_running_status1"] = std::make_shared<NumberVariable>();
        this->properties["motor_running_status2"] = std::make_shared<NumberVariable>();
        // TODO: PDOs
    } else {
        this->properties["control_mode"] = std::make_shared<NumberVariable>();
        this->properties["error_code"] = std::make_shared<NumberVariable>();
        this->properties["motor_running_status"] = std::make_shared<NumberVariable>();
        this->properties["speed"] = std::make_shared<NumberVariable>();
        // TODO: PDOs
    }
}

// TODO: this needs to be tested
void UUMotor::subscribe_to_can() {
    this->can->subscribe(this->can_id << 20, std::static_pointer_cast<Module>(this->shared_from_this()));
}

void UUMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    // Extrahiere Informationen aus der CAN ID
    uint8_t direction = (id >> 28) & 0x1;
    uint8_t slave_id = (id >> 20) & 0xFF;
    uint8_t reg_num = (id >> 16) & 0xF;
    uint16_t reg_addr = id & 0xFFFF;

    if (direction != 1 || slave_id != this->can_id || reg_num != this->register_number) {
        return;
    }
    if (this->motor_type == uu_registers::MotorType::COMBINED) {
        this->handle_combined_can_msg(reg_addr, data);
    } else {
        this->handle_single_can_msg(reg_addr, data);
    }
}
void UUMotor::handle_combined_can_msg(const uint16_t reg_addr, const uint8_t *const data) {
    const auto &reg = this->registers;
    if (reg_addr == reg.CONTROL_MODE) {
        int control_mode1;
        std::memcpy(&control_mode1, data, 2);
        this->properties.at("control_mode1")->integer_value = control_mode1;
        int control_mode2;
        std::memcpy(&control_mode2, data + 2, 2);
        this->properties.at("control_mode2")->integer_value = control_mode2;
    } else if (reg_addr == reg.ERROR_CODE) {
        int error_code1;
        std::memcpy(&error_code1, data, 2);
        this->properties.at("error_code1")->integer_value = error_code1;
        int error_code2;
        std::memcpy(&error_code2, data + 2, 2);
        this->properties.at("error_code2")->integer_value = error_code2;
    } else if (reg_addr == reg.MOTOR_RUNNING_STATUS) {
        int motor_running_status1;
        std::memcpy(&motor_running_status1, data, 2);
        this->properties.at("motor_running_status1")->integer_value = motor_running_status1;
        int motor_running_status2;
        std::memcpy(&motor_running_status2, data + 2, 2);
        this->properties.at("motor_running_status2")->integer_value = motor_running_status2;
    }
    // TODO: PDOs
}

void UUMotor::handle_single_can_msg(const uint16_t reg_addr, const uint8_t *const data) {
    const auto &reg = this->registers;
    if (reg_addr == reg.CONTROL_MODE) {
        int control_mode;
        std::memcpy(&control_mode, data, 2);
        this->properties.at("control_mode")->integer_value = control_mode;
    } else if (reg_addr == reg.ERROR_CODE) {
        int error_code;
        std::memcpy(&error_code, data, 2);
        this->properties.at("error_code")->integer_value = error_code;
    } else if (reg_addr == reg.MOTOR_RUNNING_STATUS) {
        int motor_running_status;
        std::memcpy(&motor_running_status, data, 2);
        this->properties.at("motor_running_status")->integer_value = motor_running_status;
    }
    // TODO: PDOs
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
    ESP_LOGI("UUMotor", "Sending CAN message with ID %lx, DLC %d", can_msg_id, 0);
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

    ESP_LOGI("UUMotor", "CAN ID components:");
    ESP_LOGI("UUMotor", "  Scope/Direction: 0x%lu", (can_msg_id >> 28) & 0xF);
    ESP_LOGI("UUMotor", "  Slave ID: 0x%lu", (can_msg_id >> 20) & 0xFF);
    ESP_LOGI("UUMotor", "  Register Number: 0x%lu", (can_msg_id >> 16) & 0xF);
    ESP_LOGI("UUMotor", "  Register Address: 0x%lu", can_msg_id & 0xFFFF);

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

    ESP_LOGI("UUMotor", "Sending CAN message with ID 0x%08lx, DLC %d, Data:", can_msg_id, dlc);
    char data_str[50] = {0};
    int pos = 0;
    for (int i = 0; i < dlc; i++) { // Vorwärts durch die Bytes iterieren
        pos += sprintf(data_str + pos, "%02x ", data[i]);
    }
    ESP_LOGI("UUMotor", "Data: %s", data_str);

    this->can->send(can_msg_id, data, false, dlc, true);
}

void UUMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {

    if (method_name == "speed") {
        Module::expect(arguments, 1, numbery);
        this->speed(arguments[0]->evaluate_number());
    } else if (method_name == "position") {
        Module::expect(arguments, 1, numbery);
        this->position(arguments[0]->evaluate_number());
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
        Module::expect(arguments, 2, integer, integer);
        this->can_write(arguments[0]->evaluate_integer(), uu_registers::DLC_U32, arguments[1]->evaluate_integer());
    } else if (method_name == "set_hall") {
        Module::expect(arguments, 0);
        this->set_hall();
    } else {
        Module::call(method_name, arguments);
    }
}

void UUMotor::speed(const int16_t speed) {
    if (this->properties.at("control_mode")->integer_value != uu_registers::CONTROL_MODE_SPEED) {
        this->set_mode(uu_registers::CONTROL_MODE_SPEED);
    }
    this->can_write(this->registers.MOTOR_SET_SPEED, uu_registers::DLC_U16, speed);
}

void UUMotor::position(const int32_t position) {
    if (this->properties.at("control_mode")->integer_value != uu_registers::CONTROL_MODE_POSITION) {
        this->set_mode(uu_registers::CONTROL_MODE_POSITION);
    }
    this->can_write(this->registers.MOTOR_SET_POSITION, uu_registers::DLC_U16, position);
}

void UUMotor::off() {
    this->can_write(this->registers.CONTROL_COMMAND, uu_registers::DLC_U16, uu_registers::STOP_MOTOR);
}

void UUMotor::start() {
    this->can_write(this->registers.CONTROL_COMMAND, uu_registers::DLC_U16, uu_registers::START_MOTOR);
}

void UUMotor::set_mode(const uint16_t control_mode) {
    if (this->properties.at("control_mode")->integer_value == control_mode) {
        return;
    }
    this->can_write(this->registers.CONTROL_MODE, uu_registers::DLC_U16, control_mode);
}

void UUMotor::reset_motor_error() {
    this->can_write(this->registers.CONTROL_COMMAND, uu_registers::DLC_U16, uu_registers::CLEAR_ERRORS);
}
void UUMotor::stop() {
    this->speed(0);
}

double UUMotor::get_position() {
    // TODO: Implement position reading from UU motor
    return 0.0;
}

void UUMotor::position(const double position, const double speed, const double acceleration) {
    // TODO: Implement position control for UU motor
}

double UUMotor::get_speed() {
    // TODO: Implement speed reading from UU motor
    return 0.0;
}

void UUMotor::speed(const double speed, const double acceleration) {
    // TODO: Implement speed control for UU motor
}

void UUMotor::set_hall() {
    this->can_write(this->registers.SET_HALL, uu_registers::DLC_U16, 1);
}
