#include "uu_motor.h"
#include <memory>

UUMotor_single::UUMotor_single(const std::string &name, const Can_ptr can, const uint32_t can_id, uu_registers::MotorType type)
    : UUMotor(name, can, can_id, type) {

    this->properties["control_mode"] = std::make_shared<IntegerVariable>();
    this->properties["error_code"] = std::make_shared<IntegerVariable>();
    this->properties["motor_running_status"] = std::make_shared<IntegerVariable>();
    this->properties["speed"] = std::make_shared<IntegerVariable>();
    this->properties["error_flag"] = std::make_shared<BooleanVariable>();

    // Set the PDOs for the motor
    if (motor_type == uu_registers::MotorType::MOTOR1) {
        this->setup_pdo_motor1();
    } else if (motor_type == uu_registers::MotorType::MOTOR2) {
        this->setup_pdo_motor2();
    }
}

void UUMotor_single::subscribe_to_can() {
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
    }
}

void UUMotor_single::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    uint16_t reg_addr = id & 0xFFFF; // Extract register address from CAN ID
    this->last_can_msg_time = esp_timer_get_time();
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
void UUMotor_single::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {

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

void UUMotor_single::set_speed(const int16_t speed) {

    if (this->properties.at("motor_running_status")->integer_value != uu_registers::MOTOR_RUNNING_STATUS_RUNNING) {
        this->start();
    }
    // Single motor mode
    if (this->properties.at("control_mode")->integer_value != uu_registers::CONTROL_MODE_SPEED) {
        this->set_mode(uu_registers::CONTROL_MODE_SPEED);
    }
    this->can_write(this->registers.MOTOR_SET_SPEED, uu_registers::DLC_U16, speed);
}

void UUMotor_single::set_mode(const uint16_t control_mode) {

    // Single motor mode
    if (this->properties.at("control_mode")->integer_value == control_mode) {
        return;
    }

    this->can_write(this->registers.CONTROL_MODE, uu_registers::DLC_U16, control_mode);
}

void UUMotor_single::reset_motor_error() {
    if (this->properties.at("error_flag")->boolean_value) {
        this->setup_pdo_motor1();
        this->setup_pdo_motor2();
        this->can_write(this->registers.CONTROL_COMMAND, uu_registers::DLC_U16, uu_registers::CLEAR_ERRORS);
        this->properties.at("error_flag")->boolean_value = false;
    }
}

void UUMotor_single::setup_motor() {
    this->can_write(this->registers.SET_HALL, uu_registers::DLC_U16, uu_registers::SENSOR_TYPE);
    this->can_write(this->registers.CALIBRATION, uu_registers::DLC_U16, uu_registers::START_CALIBRATION);
}

void UUMotor_single::off() {
    this->can_write(this->registers.CONTROL_COMMAND, uu_registers::DLC_U16, uu_registers::STOP_MOTOR);
}

void UUMotor_single::start() {
    this->can_write(this->registers.CONTROL_COMMAND, uu_registers::DLC_U16, uu_registers::START_MOTOR);
}

void UUMotor_single::stop() {
    this->set_speed(0);
}
