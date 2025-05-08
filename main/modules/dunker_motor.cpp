#include "dunker_motor.h"
#include "canopen.h"
#include "utils/timing.h"
#include "utils/uart.h"
#include <cinttypes>

REGISTER_MODULE_DEFAULTS(DunkerMotor)

const std::map<std::string, Variable_ptr> DunkerMotor::get_defaults() {
    return {
        {"speed", std::make_shared<NumberVariable>()},
        {"voltage_logic", std::make_shared<NumberVariable>()},
        {"voltage_power", std::make_shared<NumberVariable>()},
        {"m_per_turn", std::make_shared<NumberVariable>(1.0)},
        {"reversed", std::make_shared<BooleanVariable>()},
        {"enabled", std::make_shared<BooleanVariable>(true)},
    };
}

DunkerMotor::DunkerMotor(const std::string &name, Can_ptr can, int64_t node_id)
    : Module(dunker_motor, name), can(can), node_id(check_node_id(node_id)) {
    this->properties = DunkerMotor::get_defaults();
}

void DunkerMotor::subscribe_to_can() {
    can->subscribe(0x700 + node_id, this->shared_from_this()); // NMT response
    can->subscribe(0x580 + node_id, this->shared_from_this()); // SDO response
    can->subscribe(0x180 + node_id, this->shared_from_this()); // TPDO1

    // restart device
    this->nmt_write(0x81);

    // setup TPDO1: measured velocity
    this->sdo_write(0x1800, 1, 32, -1);
    this->sdo_write(0x1A00, 0, 8, 0);
    this->sdo_write(0x1A00, 1, 32, (0x4A04 << 16) | (2 << 8) | 32); // 0x4A04.02: measured velocity
    this->sdo_write(0x1A00, 0, 8, 1);
    this->sdo_write(0x1800, 1, 32, 0x180 + this->node_id);

    // enter operational state
    this->nmt_write(0x01);
}

void DunkerMotor::sdo_read(const uint16_t index, const uint8_t sub) {
    uint8_t data[8];
    data[0] = 0x40;
    data[1] = (index >> 0) & 0xFF;
    data[2] = (index >> 8) & 0xFF;
    data[3] = sub;
    this->can->send(0x600 + this->node_id, data);
}

void DunkerMotor::nmt_write(const uint8_t cs) {
    uint8_t data[8];
    data[0] = cs;
    data[1] = this->node_id;
    this->can->send(0x000, data);
    this->wait();
}

void DunkerMotor::sdo_write(const uint16_t index, const uint8_t sub, const uint8_t bits, const uint32_t value, const bool wait) {
    this->waiting_sdo_writes++;
    uint8_t data[8];
    data[0] = bits == 8    ? 0x2F
              : bits == 16 ? 0x2B
                           : 0x23;
    data[1] = (index >> 0) & 0xFF;
    data[2] = (index >> 8) & 0xFF;
    data[3] = sub;
    data[4] = (value >> 0) & 0xFF;
    data[5] = (value >> 8) & 0xFF;
    data[6] = (value >> 16) & 0xFF;
    data[7] = (value >> 24) & 0xFF;
    this->can->send(0x600 + this->node_id, data);
    if (wait) {
        this->wait();
    }
}

void DunkerMotor::wait() {
    const int TIMEOUT = 1000;
    const int INTERVAL = 10;
    for (int i = 0; i < TIMEOUT / INTERVAL; ++i) {
        while (this->can->receive())
            ;
        if (this->waiting_nmt_writes || this->waiting_sdo_writes) {
            return;
        }
        delay(INTERVAL);
    }
    echo("error: timeout while waiting for response");
    this->waiting_nmt_writes = 0;
    this->waiting_sdo_writes = 0;
}

void DunkerMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "sdo_read") {
        if (arguments.size() < 1 || arguments.size() > 2) {
            throw std::runtime_error("unexpected number of arguments");
        }
        expect(arguments, -1, integer, integer);
        const uint16_t index = arguments[0]->evaluate_integer();
        const uint8_t sub = arguments.size() >= 2 ? arguments[1]->evaluate_integer() : 0;
        this->sdo_read(index, sub);
    } else if (method_name == "sdo_write") {
        expect(arguments, 4, integer, integer, integer, integer);
        const uint16_t index = arguments[0]->evaluate_integer();
        const uint8_t sub = arguments[1]->evaluate_integer();
        const uint8_t bits = arguments[2]->evaluate_integer();
        const uint32_t value = arguments[3]->evaluate_integer();
        this->sdo_write(index, sub, bits, value);
    } else if (method_name == "enable") {
        Module::expect(arguments, 0);
        this->enable();
    } else if (method_name == "disable") {
        Module::expect(arguments, 0);
        this->disable();
    } else if (method_name == "speed") {
        Module::expect(arguments, 1, numbery);
        this->speed(arguments[0]->evaluate_number());
    } else if (method_name == "update_voltages") {
        Module::expect(arguments, 0);
        this->sdo_read(0x4110, 1);
        this->sdo_read(0x4111, 1);
    } else {
        Module::call(method_name, arguments);
    }
}

void DunkerMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    if (id == 0x700 + this->node_id) {
        this->waiting_nmt_writes--;
    }
    if (id == 0x580 + this->node_id) {
        this->waiting_sdo_writes--;
        if (data[0] == 0x43 && data[1] == 0x10 && data[2] == 0x41 && data[3] == 0x01) {
            this->properties["voltage_logic"]->number_value = ((data[5] << 8) | data[4]) / 1000.0;
        }
        if (data[0] == 0x43 && data[1] == 0x11 && data[2] == 0x41 && data[3] == 0x01) {
            this->properties["voltage_power"]->number_value = ((data[5] << 8) | data[4]) / 1000.0;
        }
    }
    if (id == 0x180 + this->node_id) {
        const int32_t motor_speed = demarshal_i32(data);
        this->properties["speed"]->number_value = motor_speed *
                                                  (this->properties.at("reversed")->boolean_value ? -1 : 1) *
                                                  this->properties.at("m_per_turn")->number_value /
                                                  60;
    }
}

void DunkerMotor::speed(const double speed) {
    if (!this->enabled)
        return;
    const int32_t motor_speed = speed /
                                this->properties.at("m_per_turn")->number_value /
                                (this->properties.at("reversed")->boolean_value ? -1 : 1) *
                                60;
    this->sdo_write(0x4300, 1, 32, motor_speed, false);
}

double DunkerMotor::get_speed() {
    return this->properties.at("speed")->number_value;
}

void DunkerMotor::enable() {
    this->enabled = true;
    this->properties.at("enabled")->boolean_value = true;
    this->sdo_write(0x4004, 1, 8, 1);
}

void DunkerMotor::disable() {
    this->sdo_write(0x4004, 1, 8, 0);
    this->enabled = false;
    this->properties.at("enabled")->boolean_value = false;
}

void DunkerMotor::step() {
    if (this->properties.at("enabled")->boolean_value != this->enabled) {
        if (this->properties.at("enabled")->boolean_value) {
            this->enable();
        } else {
            this->disable();
        }
    }
    Module::step();
}