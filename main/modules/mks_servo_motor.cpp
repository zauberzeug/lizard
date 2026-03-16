#include "mks_servo_motor.h"
#include <algorithm>

REGISTER_MODULE_DEFAULTS(MksServoMotor)

const std::map<std::string, Variable_ptr> MksServoMotor::get_defaults() {
    return {
        {"position", std::make_shared<NumberVariable>()},
        {"speed", std::make_shared<IntegerVariable>()},
        {"working_current", std::make_shared<IntegerVariable>(1700)},
        {"enabled", std::make_shared<BooleanVariable>(false)},
        {"position_error", std::make_shared<NumberVariable>()},
    };
}

MksServoMotor::MksServoMotor(const std::string name, const Can_ptr can, const uint16_t can_id)
    : Module(mks_servo_motor, name), can(can), can_id(can_id) {
    this->properties = MksServoMotor::get_defaults();
    this->send_working_current(1700);
}

void MksServoMotor::subscribe_to_can() {
    this->can->subscribe(this->can_id, std::static_pointer_cast<Module>(this->shared_from_this()));
}

void MksServoMotor::send(const uint8_t *data, uint8_t len) {
    uint8_t buf[8] = {0};
    uint8_t checksum = (uint8_t)(this->can_id & 0xFF);
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = data[i];
        checksum += data[i];
    }
    buf[len] = checksum & 0xFF;
    this->can->send(this->can_id, buf, false, len + 1);
}

void MksServoMotor::send_enable(bool enable) {
    uint8_t data[] = {0xF3, (uint8_t)(enable ? 0x01 : 0x00)};
    this->send(data, 2);
    this->properties.at("enabled")->boolean_value = enable;
}

void MksServoMotor::send_set_vfoc() {
    uint8_t data[] = {0x82, 0x05};
    this->send(data, 2);
}

void MksServoMotor::send_working_current(int64_t ma) {
    ma = std::clamp(ma, (int64_t)0, MAX_WORKING_CURRENT_MA);
    uint16_t val = (uint16_t)ma;
    uint8_t data[] = {0x83, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF)};
    this->send(data, 3);
    this->properties.at("working_current")->integer_value = ma;
}

void MksServoMotor::send_holding_current(int64_t pct) {
    int64_t ratio = std::clamp((pct / 10) - 1, (int64_t)0, MAX_HOLDING_RATIO);
    uint8_t data[] = {0x9B, (uint8_t)ratio};
    this->send(data, 2);
}

void MksServoMotor::send_speed_internal(int64_t speed, int64_t direction, int64_t acc) {
    speed = std::clamp(speed, (int64_t)0, MAX_SPEED);
    direction = std::clamp(direction, (int64_t)0, (int64_t)1);
    acc = std::clamp(acc, (int64_t)0, MAX_ACC);
    uint8_t byte1 = (uint8_t)((direction << 7) | ((speed >> 8) & 0x0F));
    uint8_t byte2 = (uint8_t)(speed & 0xFF);
    uint8_t data[] = {0xF6, byte1, byte2, (uint8_t)acc};
    this->send(data, 4);
}

void MksServoMotor::send_stop_internal(int64_t acc) {
    acc = std::clamp(acc, (int64_t)0, MAX_ACC);
    uint8_t data[] = {0xF6, 0x00, 0x00, (uint8_t)acc};
    this->send(data, 4);
}

void MksServoMotor::send_position_counts(int32_t counts, int64_t speed, int64_t acc) {
    speed = std::clamp(speed, (int64_t)0, MAX_SPEED);
    acc = std::clamp(acc, (int64_t)0, MAX_ACC);
    counts = std::clamp(counts, INT24_MIN, INT24_MAX);
    uint32_t raw = (uint32_t)counts;
    uint8_t p2 = (raw >> 16) & 0xFF;
    uint8_t p1 = (raw >> 8) & 0xFF;
    uint8_t p0 = raw & 0xFF;
    uint8_t data[] = {
        0xF5,
        (uint8_t)((speed >> 8) & 0xFF),
        (uint8_t)(speed & 0xFF),
        (uint8_t)acc,
        p2,
        p1,
        p0,
    };
    this->send(data, 7);
}

void MksServoMotor::send_position(double degrees, int64_t speed, int64_t acc) {
    int32_t counts = (int32_t)(degrees * COUNTS_PER_DEG);
    this->send_position_counts(counts, speed, acc);
}

void MksServoMotor::send_coord_zero() {
    uint8_t data[] = {0x92};
    this->send(data, 1);
}

void MksServoMotor::send_position_error_read() {
    uint8_t data[] = {0x39};
    this->send(data, 1);
    this->position_error_read_pending = true;
    this->position_error_read_received = false;
    this->position_error_read_sent_at = millis();
}

void MksServoMotor::step() {
    Module::step();
}

void MksServoMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "enable") {
        Module::expect(arguments, 0);
        this->send_enable(true);
    } else if (method_name == "disable") {
        Module::expect(arguments, 0);
        this->send_enable(false);
    } else if (method_name == "set_vfoc") {
        Module::expect(arguments, 0);
        this->send_set_vfoc();
    } else if (method_name == "zero") {
        Module::expect(arguments, 0);
        this->send_coord_zero();
        this->properties.at("position")->number_value = 0.0;
    } else if (method_name == "set_working_current") {
        Module::expect(arguments, 1, integer);
        this->send_working_current(arguments[0]->evaluate_integer());
    } else if (method_name == "set_holding_current") {
        Module::expect(arguments, 1, integer);
        this->send_holding_current(arguments[0]->evaluate_integer());
    } else if (method_name == "speed") {
        Module::expect(arguments, 3, integer, integer, integer);
        int64_t speed = arguments[0]->evaluate_integer();
        int64_t direction = arguments[1]->evaluate_integer();
        int64_t acc = arguments[2]->evaluate_integer();
        this->send_speed_internal(speed, direction, acc);
        this->properties.at("speed")->integer_value = speed;
    } else if (method_name == "stop") {
        Module::expect(arguments, 1, integer);
        this->send_stop_internal(arguments[0]->evaluate_integer());
        this->properties.at("speed")->integer_value = 0;
    } else if (method_name == "position") {
        Module::expect(arguments, 3, numbery, integer, integer);
        double degrees = arguments[0]->evaluate_number();
        int64_t speed = arguments[1]->evaluate_integer();
        int64_t acc = arguments[2]->evaluate_integer();
        this->send_position(degrees, speed, acc);
        this->properties.at("position")->number_value = degrees;
        this->properties.at("speed")->integer_value = speed;
    } else if (method_name == "read_position_error") {
        Module::expect(arguments, 0);
        this->send_position_error_read();
    } else {
        Module::call(method_name, arguments);
    }
}

void MksServoMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    if (count < 1) {
        return;
    }
    if (data[0] == 0x39 && count == 6) {
        // CRC validation
        uint8_t crc = (uint8_t)(this->can_id & 0xFF);
        for (int i = 0; i < 5; i++) {
            crc += data[i];
        }
        if ((crc & 0xFF) != data[5]) {
            return;
        }
        // Extract 32-bit big-endian signed position error from data[1..4]
        int32_t val = ((int32_t)data[1] << 24) |
                      ((int32_t)data[2] << 16) |
                      ((int32_t)data[3] << 8) |
                      (int32_t)data[4];
        this->position_error_value = val;
        this->position_error_read_received = true;
        this->position_error_read_pending = false;
        this->properties.at("position_error")->number_value = (double)val * 360.0 / POSITION_ERROR_COUNTS_PER_TURN;
    }
}
