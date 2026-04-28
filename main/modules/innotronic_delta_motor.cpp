#include "innotronic_delta_motor.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include <cstring>
#include <stdexcept>

REGISTER_MODULE_DEFAULTS(InnotronicDeltaMotor)

const std::map<std::string, Variable_ptr> InnotronicDeltaMotor::get_defaults() {
    auto defaults = InnotronicMotorBase::common_defaults();
    defaults["reversed"] = std::make_shared<BooleanVariable>(false);
    defaults["angle_m1"] = std::make_shared<NumberVariable>();
    defaults["angle_m2"] = std::make_shared<NumberVariable>();
    defaults["current_m1"] = std::make_shared<NumberVariable>();
    defaults["current_m2"] = std::make_shared<NumberVariable>();
    defaults["ref_result_m1"] = std::make_shared<IntegerVariable>(0);
    defaults["ref_result_m2"] = std::make_shared<IntegerVariable>(0);
    return defaults;
}

InnotronicDeltaMotor::MotorConfig InnotronicDeltaMotor::config_for(const std::string &motor_type) {
    // Known delta arm motors and their firmware operating-mode words (Configure 0x0B / setting 0x02):
    //   "windmeile"     — first delta arm,           300 ticks/rev, mode 0xB5B5
    //   "g350"          — drive motor used as delta, 600 ticks/rev, mode 0xD5D5
    //   "g250r-t-14.2"  — new delta arm 14.2:1 gear, ticks unknown, mode 0xC5C5
    // The 12.5:1 g250r-t variant is intentionally not listed — may never be implemented.
    if (motor_type == "windmeile") {
        return {300, 0xB5B5};
    }
    if (motor_type == "g350") {
        return {600, 0xD5D5};
    }
    if (motor_type == "g250r-t-14.2") {
        throw std::runtime_error("delta motor_type \"g250r-t-14.2\": ticks per revolution not yet known — measure and update innotronic_delta_motor.cpp");
    }
    throw std::runtime_error("unknown delta motor_type: " + motor_type);
}

InnotronicDeltaMotor::InnotronicDeltaMotor(const std::string name, const Can_ptr can, const uint32_t node_id,
                                           const std::string motor_type)
    : InnotronicMotorBase(innotronic_delta_motor, name, can, node_id),
      motor_ticks(config_for(motor_type).ticks) {
    this->properties = InnotronicDeltaMotor::get_defaults();
    // Switch firmware to the operating mode for this motor variant. Fire-and-forget at construction.
    this->configure(0x02, config_for(motor_type).mode, 0);
}

bool InnotronicDeltaMotor::is_reversed() const {
    return this->properties.at("reversed")->boolean_value;
}

double InnotronicDeltaMotor::sign() const {
    return this->is_reversed() ? -1.0 : 1.0;
}

void InnotronicDeltaMotor::subscribe_to_can() {
    auto self = std::static_pointer_cast<Module>(this->shared_from_this());
    this->can->subscribe((this->node_id << 5) | 0x11, self);
    this->can->subscribe((this->node_id << 5) | 0x14, self);
    this->can->subscribe((this->node_id << 5) | 0x15, self);
}

void InnotronicDeltaMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    uint8_t cmd_id = id & 0x1F;
    switch (cmd_id) {
    case 0x11:
        this->handle_status_msg(data);
        break;
    case 0x14: {
        // ReferenceFeedback: reference drive completion result.
        // Byte 0: high nibble = motor 1 result, low nibble = motor 2 result.
        // Values: 0=none, 1=OK, 2=overcurrent, 4=ref_end (motor keeps spinning)
        uint8_t ref_m1 = (data[0] >> 4) & 0x0F;
        uint8_t ref_m2 = data[0] & 0x0F;
        this->properties.at("ref_result_m1")->integer_value = ref_m1;
        this->properties.at("ref_result_m2")->integer_value = ref_m2;
        auto ref_str = [](uint8_t v) -> const char * {
            switch (v) {
            case REF_OK: return "OK";
            case REF_OVERCURRENT: return "OVERCURRENT";
            case REF_END: return "REF_END";
            default: return "NONE";
            }
        };
        echo("[%lu] CAN RX [NodeID=%ld, CmdID=0x14]: Reference Result Motor1: %s Motor2: %s",
             millis(), this->node_id, ref_str(ref_m1), ref_str(ref_m2));
        break;
    }
    case 0x15: {
        // CurrentAngleCurrent: per-motor angle and current in position-controller mode.
        // Bytes: [0-1] angle_m1 int16 hall ticks, [2-3] angle_m2 int16 hall ticks,
        //        [4-5] current_m1 int16 mA, [6-7] current_m2 int16 mA
        int16_t raw_angle_m1, raw_angle_m2, raw_current_m1, raw_current_m2;
        std::memcpy(&raw_angle_m1, data, 2);
        std::memcpy(&raw_angle_m2, data + 2, 2);
        std::memcpy(&raw_current_m1, data + 4, 2);
        std::memcpy(&raw_current_m2, data + 6, 2);
        this->properties.at("angle_m1")->number_value = raw_angle_m1;
        this->properties.at("angle_m2")->number_value = raw_angle_m2;
        this->properties.at("current_m1")->number_value = raw_current_m1 * 0.001;
        this->properties.at("current_m2")->number_value = raw_current_m2 * 0.001;
        if (this->is_debug()) {
            echo("CAN RX [NodeID=%ld, CmdID=0x15]: angle_m1=%d angle_m2=%d current_m1=%.2fA current_m2=%.2fA",
                 this->node_id, raw_angle_m1, raw_angle_m2,
                 this->properties.at("current_m1")->number_value,
                 this->properties.at("current_m2")->number_value);
        }
        break;
    }
    }
}

void InnotronicDeltaMotor::send_rel_angle_cmd(float angle, uint16_t vel_limit, uint8_t acc_limit, int8_t jerk_limit_exp) {
    // RelAngleCmd 0x02: move by relative angle.
    // Bytes: [0-1] angle int16 0.001 rad, [2-3] vel_limit uint16 0.01 rad/s (0xFFFF=none),
    //        [4] acc_limit, [5] jerk_limit_exp
    if (!this->is_enabled()) {
        return;
    }
    int16_t raw_angle = static_cast<int16_t>(angle * this->sign() / 0.001f);
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(data, &raw_angle, 2);
    std::memcpy(data + 2, &vel_limit, 2);
    data[4] = acc_limit;
    data[5] = static_cast<uint8_t>(jerk_limit_exp);
    uint32_t can_id = (this->node_id << 5) | 0x02;
    this->can->send(can_id, data);
}

void InnotronicDeltaMotor::send_delta_angle_cmd(uint8_t motor_select, int16_t pos1, uint8_t spd1, int16_t pos2, uint8_t spd2) {
    // AngleCmd 0x03: per-motor position command.
    // Byte 0: motor_select (0x10=left, 0x20=right, 0x30=both)
    // Bytes 1-2: pos1 int16 hall ticks (±150 = ±180°), Byte 3: spd1 (1=fast..50=slow)
    // Bytes 4-5: pos2 (only if 0x30), Byte 6: spd2
    if (!this->is_enabled()) {
        return;
    }
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    data[0] = motor_select;
    std::memcpy(data + 1, &pos1, 2);
    data[3] = spd1;
    if (motor_select == 0x30) {
        std::memcpy(data + 4, &pos2, 2);
        data[6] = spd2;
    }
    uint32_t can_id = (this->node_id << 5) | 0x03;
    this->can->send(can_id, data);
}

void InnotronicDeltaMotor::send_reference_drive(uint8_t motor, uint8_t cmd) {
    // SingleMotorControl 0x0C used for reference drive.
    // Byte 0: motor 1 cmd, Byte 1: motor 2 cmd
    // 0x00=no action, 0x05=brake (stores current pos as 0-ref if ref drive active),
    // 0x10=calibration CW, 0x20=calibration CCW
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    data[0] = (motor == 1) ? cmd : 0x00;
    data[1] = (motor == 2) ? cmd : 0x00;
    uint32_t can_id = (this->node_id << 5) | 0x0C;
    this->can->send(can_id, data);
}

void InnotronicDeltaMotor::reference_drive_start(uint8_t motor, bool clockwise) {
    this->send_reference_drive(motor, clockwise ? 0x10 : 0x20);
}

void InnotronicDeltaMotor::reference_drive_stop(uint8_t motor) {
    if (this->is_debug()) {
        echo("%s: stopping reference drive on motor %d", this->name.c_str(), motor);
    }
    this->send_reference_drive(motor, 0x05);
}

void InnotronicDeltaMotor::send_single_motor_control(uint8_t cmd_motor1, uint8_t cmd_motor2) {
    // SingleMotorControl 0x0C: arbitrary cmd combinations on both motor slots.
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    data[0] = cmd_motor1;
    data[1] = cmd_motor2;
    uint32_t can_id = (this->node_id << 5) | 0x0C;
    this->can->send(can_id, data);
}

void InnotronicDeltaMotor::stop() {
    this->send_switch_state(2);
}

void InnotronicDeltaMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "rel_angle") {
        if (arguments.size() < 1 || arguments.size() > 4) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, numbery, numbery, numbery, numbery);
        float angle = arguments[0]->evaluate_number();
        uint16_t vel_limit = arguments.size() > 1 ? static_cast<uint16_t>(arguments[1]->evaluate_number() / 0.01) : 0xFFFF;
        uint8_t acc_limit = arguments.size() > 2 ? static_cast<uint8_t>(arguments[2]->evaluate_number()) : 0x00;
        int8_t jerk_limit_exp = arguments.size() > 3 ? static_cast<int8_t>(arguments[3]->evaluate_number()) : (int8_t)0x00;
        this->send_rel_angle_cmd(angle, vel_limit, acc_limit, jerk_limit_exp);
    } else if (method_name == "delta_angle") {
        if (arguments.size() < 2 || arguments.size() > 5) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, integer, integer, integer, integer, integer);
        uint8_t motor_select = static_cast<uint8_t>(arguments[0]->evaluate_integer());
        int16_t pos1 = static_cast<int16_t>(arguments[1]->evaluate_integer());
        uint8_t spd1 = arguments.size() > 2 ? static_cast<uint8_t>(arguments[2]->evaluate_integer()) : 10;
        int16_t pos2 = arguments.size() > 3 ? static_cast<int16_t>(arguments[3]->evaluate_integer()) : 0;
        uint8_t spd2 = arguments.size() > 4 ? static_cast<uint8_t>(arguments[4]->evaluate_integer()) : 10;
        this->send_delta_angle_cmd(motor_select, pos1, spd1, pos2, spd2);
    } else if (method_name == "reference_drive_start") {
        if (arguments.size() < 1 || arguments.size() > 2) {
            throw std::runtime_error("unexpected number of arguments");
        }
        Module::expect(arguments, -1, integer, boolean);
        uint8_t motor = static_cast<uint8_t>(arguments[0]->evaluate_integer());
        bool clockwise = arguments.size() > 1 ? arguments[1]->evaluate_boolean() : true;
        this->reference_drive_start(motor, clockwise);
    } else if (method_name == "reference_drive_stop") {
        Module::expect(arguments, 1, integer);
        this->reference_drive_stop(static_cast<uint8_t>(arguments[0]->evaluate_integer()));
    } else if (method_name == "single_motor_control") {
        Module::expect(arguments, 2, integer, integer);
        uint8_t cmd_motor1 = static_cast<uint8_t>(arguments[0]->evaluate_integer());
        uint8_t cmd_motor2 = static_cast<uint8_t>(arguments[1]->evaluate_integer());
        this->send_single_motor_control(cmd_motor1, cmd_motor2);
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->stop();
    } else {
        InnotronicMotorBase::call(method_name, arguments);
    }
}
