#include "mks_servo_motor.h"
#include "../utils/uart.h"
#include <cstring>

REGISTER_MODULE_DEFAULTS(MksServoMotor)

const std::map<std::string, Variable_ptr> MksServoMotor::get_defaults() {
    return {
        {"position", std::make_shared<NumberVariable>()},
        {"speed", std::make_shared<IntegerVariable>()},
        {"working_current", std::make_shared<IntegerVariable>(1700)},
        {"enabled", std::make_shared<BooleanVariable>(false)},
        {"homing_state", std::make_shared<IntegerVariable>(0)},
        {"homing_active", std::make_shared<BooleanVariable>(false)},
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
    if (ma < 0) {
        ma = 0;
    }
    if (ma > 3000) {
        ma = 3000;
    }
    uint16_t val = (uint16_t)ma;
    uint8_t data[] = {0x83, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF)};
    this->send(data, 3);
    this->properties.at("working_current")->integer_value = ma;
}

void MksServoMotor::send_holding_current(int64_t pct) {
    int64_t ratio = (pct / 10) - 1;
    if (ratio < 0) {
        ratio = 0;
    }
    if (ratio > 9) {
        ratio = 9;
    }
    uint8_t data[] = {0x9B, (uint8_t)ratio};
    this->send(data, 2);
}

void MksServoMotor::send_run_internal(int64_t direction, int64_t speed, int64_t acc) {
    if (speed < 0) {
        speed = 0;
    }
    if (speed > 4095) {
        speed = 4095;
    }
    if (direction < 0) {
        direction = 0;
    }
    if (direction > 1) {
        direction = 1;
    }
    if (acc < 0) {
        acc = 0;
    }
    if (acc > 255) {
        acc = 255;
    }
    uint8_t byte1 = (uint8_t)((direction << 7) | ((speed >> 8) & 0x0F));
    uint8_t byte2 = (uint8_t)(speed & 0xFF);
    uint8_t data[] = {0xF6, byte1, byte2, (uint8_t)acc};
    this->send(data, 4);
}

void MksServoMotor::send_stop_internal(int64_t acc) {
    if (acc < 0) {
        acc = 0;
    }
    if (acc > 255) {
        acc = 255;
    }
    uint8_t data[] = {0xF6, 0x00, 0x00, (uint8_t)acc};
    this->send(data, 4);
}

void MksServoMotor::send_rotate_counts(int32_t counts, int64_t speed, int64_t acc) {
    if (speed < 0) {
        speed = 0;
    }
    if (speed > 65535) {
        speed = 65535;
    }
    if (acc < 0) {
        acc = 0;
    }
    if (acc > 255) {
        acc = 255;
    }
    if (counts > INT24_MAX) {
        counts = INT24_MAX;
    }
    if (counts < INT24_MIN) {
        counts = INT24_MIN;
    }
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

void MksServoMotor::send_rotate(double degrees, int64_t speed, int64_t acc) {
    int32_t counts = (int32_t)(degrees * COUNTS_PER_DEG);
    this->send_rotate_counts(counts, speed, acc);
}

void MksServoMotor::send_coord_zero() {
    uint8_t data[] = {0x92};
    this->send(data, 1);
}

void MksServoMotor::send_angle_error_read() {
    uint8_t data[] = {0x39};
    this->send(data, 1);
    this->angle_error_read_pending = true;
    this->angle_error_read_received = false;
    this->angle_error_read_sent_at = millis();
}

void MksServoMotor::step() {
    Module::step();

    if (this->pz_state != PZ_IDLE) {
        this->step_precision_zero();
    }
}

void MksServoMotor::step_precision_zero() {
    switch (this->pz_state) {
    case PZ_FIRST_ROTATE:
        this->send_working_current(1000);
        this->send_rotate(this->pz_target_degrees, this->pz_speed, this->pz_acc);
        this->pz_phase_start = millis();
        this->pz_state = PZ_WAIT_FIRST_ROTATE;
        break;
    case PZ_WAIT_FIRST_ROTATE:
        if (millis_since(this->pz_phase_start) >= this->pz_rotate_wait_ms) {
            this->pz_state = PZ_READ_ERROR;
        }
        break;
    case PZ_READ_ERROR:
        this->angle_error_read_retries = 0;
        this->send_angle_error_read();
        this->pz_state = PZ_WAIT_ERROR;
        break;
    case PZ_WAIT_ERROR:
        if (this->angle_error_read_received) {
            int32_t abs_error = this->angle_error_value < 0 ? -this->angle_error_value : this->angle_error_value;
            double error_degrees = (double)abs_error * 360.0 / 51200.0;
            double corrected = this->pz_target_degrees - error_degrees;
            this->send_rotate(corrected, this->pz_speed, this->pz_acc);
            this->pz_phase_start = millis();
            this->pz_state = PZ_CORRECT_ROTATE;
        } else if (millis_since(this->angle_error_read_sent_at) > 200) {
            if (this->angle_error_read_retries < 5) {
                this->angle_error_read_retries++;
                this->send_angle_error_read();
            } else {
                this->pz_state = PZ_FAILED;
                this->properties.at("homing_state")->integer_value = PZ_FAILED;
            }
        }
        break;
    case PZ_CORRECT_ROTATE:
        this->pz_state = PZ_WAIT_CORRECT_ROTATE;
        this->pz_phase_start = millis();
        break;
    case PZ_WAIT_CORRECT_ROTATE:
        if (millis_since(this->pz_phase_start) >= this->pz_correct_wait_ms) {
            this->pz_state = PZ_SET_ZERO;
        }
        break;
    case PZ_SET_ZERO:
        this->send_coord_zero();
        this->properties.at("position")->number_value = 0.0;
        this->pz_phase_start = millis();
        this->pz_state = PZ_WAIT_AFTER_ZERO;
        break;
    case PZ_WAIT_AFTER_ZERO:
        if (millis_since(this->pz_phase_start) >= 1000) {
            this->pz_state = PZ_MOVE_TO_START;
        }
        break;
    case PZ_MOVE_TO_START:
        this->send_rotate(-110.0, this->pz_speed, this->pz_acc);
        this->properties.at("position")->number_value = -110.0;
        this->pz_state = PZ_DONE;
        this->properties.at("homing_state")->integer_value = PZ_DONE;
        this->properties.at("homing_active")->boolean_value = false;
        break;
    default:
        break;
    }
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
    } else if (method_name == "precision_zero") {
        Module::expect(arguments, 0);
        this->pz_state = PZ_FIRST_ROTATE;
        this->properties.at("homing_active")->boolean_value = true;
        this->properties.at("homing_state")->integer_value = PZ_FIRST_ROTATE;
    } else if (method_name == "set_working_current") {
        Module::expect(arguments, 1, integer);
        this->send_working_current(arguments[0]->evaluate_integer());
    } else if (method_name == "set_holding_current") {
        Module::expect(arguments, 1, integer);
        this->send_holding_current(arguments[0]->evaluate_integer());
    } else if (method_name == "run") {
        Module::expect(arguments, 3, integer, integer, integer);
        int64_t speed = arguments[0]->evaluate_integer();
        int64_t direction = arguments[1]->evaluate_integer();
        int64_t acc = arguments[2]->evaluate_integer();
        this->send_run_internal(direction, speed, acc);
        this->properties.at("speed")->integer_value = speed;
    } else if (method_name == "stop") {
        Module::expect(arguments, 1, integer);
        this->send_stop_internal(arguments[0]->evaluate_integer());
        this->properties.at("speed")->integer_value = 0;
    } else if (method_name == "grip") {
        Module::expect(arguments, 0);
        this->send_rotate(5.0, 3000, 3000);
        this->properties.at("position")->number_value = 5.0;
        this->properties.at("speed")->integer_value = 3000;
    } else if (method_name == "release") {
        Module::expect(arguments, 0);
        this->send_rotate(-40.0, 3000, 3000);
        this->properties.at("position")->number_value = -40.0;
        this->properties.at("speed")->integer_value = 3000;
    } else if (method_name == "rotate") {
        Module::expect(arguments, 3, numbery, integer, integer);
        double degrees = arguments[0]->evaluate_number();
        int64_t speed = arguments[1]->evaluate_integer();
        int64_t acc = arguments[2]->evaluate_integer();
        this->send_rotate(degrees, speed, acc);
        this->properties.at("position")->number_value = degrees;
        this->properties.at("speed")->integer_value = speed;
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
        // Extract 32-bit big-endian signed angle error from data[1..4]
        int32_t val = ((int32_t)data[1] << 24) |
                      ((int32_t)data[2] << 16) |
                      ((int32_t)data[3] << 8) |
                      (int32_t)data[4];
        this->angle_error_value = val;
        this->angle_error_read_received = true;
        this->angle_error_read_pending = false;
    }
}
