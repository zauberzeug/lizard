#include "innotronic_motor_base.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include <cstring>

const std::map<std::string, Variable_ptr> InnotronicMotorBase::common_defaults() {
    return {
        {"voltage", std::make_shared<NumberVariable>()},
        {"temperature", std::make_shared<IntegerVariable>()},
        {"state", std::make_shared<IntegerVariable>()},
        {"error_codes", std::make_shared<StringVariable>("0x0000")},
        {"version", std::make_shared<IntegerVariable>(0)},
        {"enabled", std::make_shared<BooleanVariable>(true)},
        {"debug", std::make_shared<BooleanVariable>(false)},
    };
}

InnotronicMotorBase::InnotronicMotorBase(const ModuleType type, const std::string name, const Can_ptr can, const uint32_t node_id)
    : Module(type, name), node_id(node_id), can(can) {
}

bool InnotronicMotorBase::is_enabled() const {
    return this->properties.at("enabled")->boolean_value;
}

bool InnotronicMotorBase::is_debug() const {
    return this->properties.at("debug")->boolean_value;
}

void InnotronicMotorBase::handle_status_msg(const uint8_t *data) {
    // Status (cyclic): bus voltage, motor velocity (ignored at base level), temperature, state, error codes, fw version.
    // Bytes: [0-1] vel int16 0.01 rad/s (subclass-specific use), [2-3] voltage int16 0.001 V,
    //        [4] temp int8 °C, [5] state, [6] error mask, [7] fw version
    int16_t raw_voltage;
    std::memcpy(&raw_voltage, data + 2, 2);
    this->properties.at("voltage")->number_value = raw_voltage * 0.001;
    this->properties.at("temperature")->integer_value = static_cast<int8_t>(data[4]);
    this->properties.at("state")->integer_value = data[5];
    char hex_buf[7];
    snprintf(hex_buf, sizeof(hex_buf), "0x%04x", data[6]);
    this->properties.at("error_codes")->string_value = hex_buf;
    this->properties.at("version")->integer_value = data[7];
    if (this->is_debug()) {
        echo("[%lu] CAN RX [NodeID=%ld, CmdID=0x11]: voltage %.2f V, temp %d C, state %d, error %s, version %d",
             millis(), this->node_id,
             this->properties.at("voltage")->number_value,
             (int)this->properties.at("temperature")->integer_value,
             (int)this->properties.at("state")->integer_value,
             hex_buf,
             (int)data[7]);
    }
}

void InnotronicMotorBase::send_switch_state(uint8_t state) {
    // SwitchState 0x0A: Byte 0 = state (1=off, 2=stop/brake, 3=on)
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    data[0] = state;
    uint32_t can_id = (this->node_id << 5) | 0x0A;
    this->can->send(can_id, data);
}

void InnotronicMotorBase::configure(uint8_t setting_id, uint16_t value1, int32_t value2) {
    // Configure 0x0B: Byte 0 = setting_id, Bytes 2-3 = value1 u16, Bytes 4-7 = value2 i32
    // setting_id: 0x00=ACK, 0x01=Set CanID, 0x02=Switch Operating Mode
    uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    data[0] = setting_id;
    std::memcpy(data + 2, &value1, 2);
    std::memcpy(data + 4, &value2, 4);
    uint32_t can_id = (this->node_id << 5) | 0x0B;
    this->can->send(can_id, data);
}

void InnotronicMotorBase::configure_node_id(uint8_t new_node_id) {
    // new_node_id is left-shifted by 5 to form the CAN base address (e.g. 1 -> 0x020).
    this->configure(0x01, static_cast<uint16_t>(new_node_id) << 5, 0);
}

void InnotronicMotorBase::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "off") {
        Module::expect(arguments, 0);
        this->send_switch_state(1);
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->send_switch_state(2);
    } else if (method_name == "on") {
        Module::expect(arguments, 0);
        this->send_switch_state(3);
    } else if (method_name == "switch_state") {
        Module::expect(arguments, 1, integer);
        this->send_switch_state(static_cast<uint8_t>(arguments[0]->evaluate_integer()));
    } else if (method_name == "configure") {
        Module::expect(arguments, 3, integer, integer, integer);
        this->configure(static_cast<uint8_t>(arguments[0]->evaluate_integer()),
                        static_cast<uint16_t>(arguments[1]->evaluate_integer()),
                        static_cast<int32_t>(arguments[2]->evaluate_integer()));
    } else if (method_name == "configure_node_id") {
        Module::expect(arguments, 1, integer);
        this->configure_node_id(static_cast<uint8_t>(arguments[0]->evaluate_integer()));
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

void InnotronicMotorBase::step() {
    bool desired = this->is_enabled();
    if (desired != this->last_applied_enabled) {
        if (desired) {
            this->enable();
        } else {
            this->disable();
        }
    }
    Module::step();
}

void InnotronicMotorBase::enable() {
    this->properties.at("enabled")->boolean_value = true;
    this->last_applied_enabled = true;
    this->send_switch_state(3);
}

void InnotronicMotorBase::disable() {
    this->send_switch_state(1);
    this->properties.at("enabled")->boolean_value = false;
    this->last_applied_enabled = false;
}
