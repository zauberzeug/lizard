#include "mks_servo_motor.h"
#include "../global.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include <cstring>
#include <memory>

REGISTER_MODULE_DEFAULTS(MksServoMotor)

const std::map<std::string, Variable_ptr> MksServoMotor::get_defaults() {
    return {
        {"status", std::make_shared<IntegerVariable>()},
        {"position", std::make_shared<NumberVariable>()},
        {"speed", std::make_shared<NumberVariable>()},
        {"can_age", std::make_shared<NumberVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
    };
}

MksServoMotor::MksServoMotor(const std::string name, const Can_ptr can, const uint8_t can_id)
    : Module(mks_servo_motor, name), can_id(can_id), can(can) {
    this->properties = MksServoMotor::get_defaults();
}

void MksServoMotor::subscribe_to_can() {
    can->subscribe(this->can_id, std::static_pointer_cast<Module>(this->shared_from_this()));
}

uint8_t MksServoMotor::calc_crc(const uint8_t *data, const uint8_t len) const {
    // CRC = (CAN_ID + all data bytes) & 0xFF
    // The CAN ID is always included in the CRC but NOT in the data field
    uint8_t crc = this->can_id;
    for (uint8_t i = 0; i < len; i++) {
        crc += data[i];
    }
    return crc & 0xFF;
}

void MksServoMotor::send(const uint8_t *data, const uint8_t len) const {
    uint8_t frame[8] = {0};
    uint8_t frame_len = len < 8 ? len : 8;
    std::memcpy(frame, data, frame_len);
    this->can->send(this->can_id, frame, false, frame_len);
}

void MksServoMotor::step() {
    this->properties.at("can_age")->number_value = millis_since(this->last_msg_millis) / 1e3;

    if (this->properties.at("enabled")->boolean_value != this->enabled) {
        if (this->properties.at("enabled")->boolean_value) {
            this->enable();
        } else {
            this->disable();
        }
    }

    Module::step();
}

void MksServoMotor::set_mode(const uint8_t mode) {
    // Command 0x82: Set working mode (DLC=3)
    // Modes: 0=CR_OPEN, 1=CR_CLOSE, 2=CR_vFOC, 3=SR_OPEN, 4=SR_CLOSE, 5=SR_vFOC
    uint8_t payload[] = {0x82, mode};
    uint8_t frame[] = {0x82, mode, calc_crc(payload, 2)};
    this->send(frame, 3);
}

void MksServoMotor::set_working_current(const uint16_t current_ma) {
    // Command 0x83: Set working current in mA (DLC=4)
    // SERVO42D/28D/35D max=3000mA, SERVO57D max=5200mA
    uint8_t payload[] = {
        0x83,
        (uint8_t)((current_ma >> 8) & 0xFF),
        (uint8_t)(current_ma & 0xFF),
    };
    uint8_t frame[] = {
        0x83,
        (uint8_t)((current_ma >> 8) & 0xFF),
        (uint8_t)(current_ma & 0xFF),
        calc_crc(payload, 3),
    };
    this->send(frame, 4);
}

void MksServoMotor::set_holding_current(const uint8_t ratio) {
    // Command 0x9B: Set holding current percentage (DLC=3)
    // ratio: 0=10%, 1=20%, 2=30%, ... 8=90%
    uint8_t payload[] = {0x9B, ratio};
    uint8_t frame[] = {0x9B, ratio, calc_crc(payload, 2)};
    this->send(frame, 3);
}

void MksServoMotor::move_to_position(const uint16_t speed, const uint8_t acceleration, const int32_t abs_pulses) {
    // Command 0xFE: Absolute motion based on pulse count (DLC=8)
    // speed: 0-3000 RPM (uint16_t)
    // acceleration: 0-255
    // abs_pulses: int24_t (-8388607 to +8388607)
    uint8_t payload[] = {
        0xFE,
        (uint8_t)((speed >> 8) & 0xFF),
        (uint8_t)(speed & 0xFF),
        acceleration,
        (uint8_t)((abs_pulses >> 16) & 0xFF),
        (uint8_t)((abs_pulses >> 8) & 0xFF),
        (uint8_t)(abs_pulses & 0xFF),
    };
    uint8_t frame[] = {
        0xFE,
        (uint8_t)((speed >> 8) & 0xFF),
        (uint8_t)(speed & 0xFF),
        acceleration,
        (uint8_t)((abs_pulses >> 16) & 0xFF),
        (uint8_t)((abs_pulses >> 8) & 0xFF),
        (uint8_t)(abs_pulses & 0xFF),
        calc_crc(payload, 7),
    };
    this->send(frame, 8);
}

void MksServoMotor::run_speed(const uint8_t direction, const uint16_t speed, const uint8_t acceleration) {
    // Command 0xF6: Speed control mode (DLC=5)
    // direction: 0=CCW, 1=CW (stored in bit7 of byte2)
    // speed: 0-3000 RPM (lower 12 bits packed across byte2[3:0] and byte3)
    // acceleration: 0-255
    uint8_t byte2 = ((direction & 0x01) << 7) | ((speed >> 8) & 0x0F);
    uint8_t byte3 = speed & 0xFF;
    uint8_t payload[] = {0xF6, byte2, byte3, acceleration};
    uint8_t frame[] = {0xF6, byte2, byte3, acceleration, calc_crc(payload, 4)};
    this->send(frame, 5);
}

void MksServoMotor::grip(const uint16_t working_current_ma, const uint8_t holding_ratio) {
    // Convenience: set FOC torque mode + working/holding current for gripping
    this->set_mode(5); // SR_vFOC (bus FOC torque mode)
    this->set_working_current(working_current_ma);
    this->set_holding_current(holding_ratio);
}

void MksServoMotor::stop() {
    // Command 0xF7: Emergency stop (DLC=2)
    uint8_t payload[] = {0xF7};
    uint8_t frame[] = {0xF7, calc_crc(payload, 1)};
    this->send(frame, 2);
}

void MksServoMotor::query_status() {
    // Command 0xF1: Read motor operating status (DLC=2)
    uint8_t payload[] = {0xF1};
    uint8_t frame[] = {0xF1, calc_crc(payload, 1)};
    this->send(frame, 2);
}

void MksServoMotor::read_encoder() {
    // Command 0x30: Read multi-turn encoder value (DLC=2)
    uint8_t payload[] = {0x30};
    uint8_t frame[] = {0x30, calc_crc(payload, 1)};
    this->send(frame, 2);
}

void MksServoMotor::read_speed() {
    // Command 0x32: Read real-time speed in RPM (DLC=2)
    uint8_t payload[] = {0x32};
    uint8_t frame[] = {0x32, calc_crc(payload, 1)};
    this->send(frame, 2);
}

void MksServoMotor::clear_stall() {
    // Command 0x3D: Release motor from stall state (DLC=2)
    uint8_t payload[] = {0x3D};
    uint8_t frame[] = {0x3D, calc_crc(payload, 1)};
    this->send(frame, 2);
}

void MksServoMotor::set_zero() {
    // Command 0x92: Set current position as zero point (DLC=2)
    uint8_t payload[] = {0x92};
    uint8_t frame[] = {0x92, calc_crc(payload, 1)};
    this->send(frame, 2);
}

void MksServoMotor::home(const uint16_t working_current_ma, const uint8_t holding_ratio) {
    // 1. Grip: move to closed position using torque mode
    this->grip(working_current_ma, holding_ratio);
    delay(1000);

    // 2. Release: disable motor so springs pull back to base position
    //    (send disable without emergency stop to just loose the shaft)
    uint8_t payload[] = {0xF3, 0x00};
    uint8_t frame[] = {0xF3, 0x00, calc_crc(payload, 2)};
    this->send(frame, 3);
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;

    // 3. Wait for springs to return motor to base position
    delay(2000);

    // 4. Set current position as zero
    this->set_zero();
    delay(100);

    // 5. Re-enable motor
    this->enable();
}

void MksServoMotor::enable() {
    // Command 0xF3: Enable motor (DLC=3)
    uint8_t payload[] = {0xF3, 0x01};
    uint8_t frame[] = {0xF3, 0x01, calc_crc(payload, 2)};
    this->send(frame, 3);
    this->enabled = true;
    this->properties.at("enabled")->boolean_value = true;
}

void MksServoMotor::disable() {
    this->stop();
    // Command 0xF3: Disable motor (DLC=3)
    uint8_t payload[] = {0xF3, 0x00};
    uint8_t frame[] = {0xF3, 0x00, calc_crc(payload, 2)};
    this->send(frame, 3);
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}

void MksServoMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "set_mode") {
        Module::expect(arguments, 1, integer);
        this->set_mode(arguments[0]->evaluate_integer());
    } else if (method_name == "set_working_current") {
        Module::expect(arguments, 1, integer);
        this->set_working_current(arguments[0]->evaluate_integer());
    } else if (method_name == "set_holding_current") {
        Module::expect(arguments, 1, integer);
        this->set_holding_current(arguments[0]->evaluate_integer());
    } else if (method_name == "move_to_position") {
        Module::expect(arguments, 3, integer, integer, integer);
        this->move_to_position(
            arguments[0]->evaluate_integer(),
            arguments[1]->evaluate_integer(),
            arguments[2]->evaluate_integer());
    } else if (method_name == "run_speed") {
        Module::expect(arguments, 3, integer, integer, integer);
        this->run_speed(
            arguments[0]->evaluate_integer(),
            arguments[1]->evaluate_integer(),
            arguments[2]->evaluate_integer());
    } else if (method_name == "grip") {
        Module::expect(arguments, 2, integer, integer);
        this->grip(
            arguments[0]->evaluate_integer(),
            arguments[1]->evaluate_integer());
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->stop();
    } else if (method_name == "query_status") {
        Module::expect(arguments, 0);
        this->query_status();
    } else if (method_name == "read_encoder") {
        Module::expect(arguments, 0);
        this->read_encoder();
    } else if (method_name == "read_speed") {
        Module::expect(arguments, 0);
        this->read_speed();
    } else if (method_name == "clear_stall") {
        Module::expect(arguments, 0);
        this->clear_stall();
    } else if (method_name == "set_zero") {
        Module::expect(arguments, 0);
        this->set_zero();
    } else if (method_name == "home") {
        Module::expect(arguments, 2, integer, integer);
        this->home(
            arguments[0]->evaluate_integer(),
            arguments[1]->evaluate_integer());
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

void MksServoMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    // Uplink format: [code, data..., CRC]
    // data[0] = command code
    if (count < 2) {
        return;
    }

    const uint8_t cmd = data[0];

    switch (cmd) {
    case 0x30: {
        // Encoder value response: [0x30, carry(4 bytes), value(2 bytes), CRC]
        // carry (int32_t) + value (uint16_t) = multi-turn encoder
        if (count >= 8) {
            int32_t carry = ((int32_t)data[1] << 24) | ((int32_t)data[2] << 16) |
                            ((int32_t)data[3] << 8) | data[4];
            uint16_t value = ((uint16_t)data[5] << 8) | data[6];
            this->properties.at("position")->number_value = (double)carry * 0x4000 + value;
        }
        break;
    }
    case 0x32: {
        // Speed response: [0x32, speed_hi, speed_lo, CRC]
        if (count >= 4) {
            int16_t speed = ((int16_t)data[1] << 8) | data[2];
            this->properties.at("speed")->number_value = speed;
        }
        break;
    }
    case 0xF1: {
        // Motor status: [0xF1, status, CRC]
        // 0=query failed, 1=stop, 2=speed up, 3=speed down, 4=full speed, 5=homing, 6=calibration
        if (count >= 3) {
            this->properties.at("status")->integer_value = data[1];
        }
        break;
    }
    case 0xF6:
    case 0xFD:
    case 0xFE: {
        // Motion command response: [code, status, CRC]
        // 0=fail, 1=starting, 2=complete, 3=end limit stopped
        if (count >= 3) {
            this->properties.at("status")->integer_value = data[1];
        }
        break;
    }
    case 0xF7: {
        // Emergency stop response: [0xF7, status, CRC]
        if (count >= 3) {
            this->properties.at("status")->integer_value = data[1];
        }
        break;
    }
    case 0xF3: {
        // Enable/disable response: [0xF3, status, CRC]
        if (count >= 3) {
            this->properties.at("status")->integer_value = data[1];
        }
        break;
    }
    case 0x82:
    case 0x83:
    case 0x9B: {
        // Config command responses: [code, status, CRC]
        if (count >= 3) {
            this->properties.at("status")->integer_value = data[1];
        }
        break;
    }
    }

    this->last_msg_millis = millis();
}
