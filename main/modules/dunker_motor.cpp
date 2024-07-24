#include "dunker_motor.h"
#include "canopen.h"
#include "uart.h"
#include "utils/timing.h"
#include <cinttypes>

DunkerMotor::DunkerMotor(const std::string &name, Can_ptr can, int64_t node_id)
    : Module(dunker_motor, name), can(can), node_id(check_node_id(node_id)) {
    this->properties["m_per_tick"] = std::make_shared<NumberVariable>(1.0);
    this->properties["reversed"] = std::make_shared<BooleanVariable>();
    this->properties["shaft_federate"] = std::make_shared<NumberVariable>(50);
    this->properties["shaft_revolutions"] = std::make_shared<NumberVariable>(30);
    this->properties["switch_search_speed"] = std::make_shared<NumberVariable>(3);
    this->properties["zero_search_speed"] = std::make_shared<NumberVariable>(3);
    this->properties["homing_acceleration"] = std::make_shared<NumberVariable>(50);
    this->properties["homing_method"] = std::make_shared<NumberVariable>(18);
    this->properties["homing_offset"] = std::make_shared<NumberVariable>(1000);
    this->properties["acceleration"] = std::make_shared<NumberVariable>(100);
    this->properties["position"] = std::make_shared<NumberVariable>();
    this->properties["velocity"] = std::make_shared<NumberVariable>();
    this->properties["statusword"] = std::make_shared<NumberVariable>(-1);
    this->properties["status_flag"] = std::make_shared<NumberVariable>();
    this->properties["PDO1"] = std::make_shared<NumberVariable>();
}

void DunkerMotor::subscribe_to_can() {
    can->subscribe(0x700 + node_id, this->shared_from_this()); // NMT response
    can->subscribe(0x580 + node_id, this->shared_from_this()); // SDO response
    can->subscribe(0x180 + node_id, this->shared_from_this()); // RPDO1
    can->subscribe(0x201 + node_id, this->shared_from_this()); // RPDO2
    // this->nmt_write(0x01);
    // // setup RPDO
    // // this->sdo_write(0x1800, 1, 32, -1);
    // // this->sdo_write(0x1A00, 0, 8, 0);
    // // this->sdo_write(0x1A00, 1, 32, (0x6041 << 16) | (0 << 8) | 16);
    // // this->sdo_write(0x1A00, 0, 8, 1);
    // // this->sdo_write(0x1800, 1, 32, 0x200 + this->node_id);
    // this->sdo_write(0x1800, 1, 32, 0x80000000);            // clear RPDO
    // this->sdo_write(0x1A00, 0, 8, 0);                      // clear entries
    // this->sdo_write(0x1A00, 1, 32, 0x60400010);            // statusword
    // this->sdo_write(0x1A00, 2, 32, 0x20140020);            // status flag(referenced)
    // this->sdo_write(0x1A00, 0, 8, 2);                      // number of entries
    // this->sdo_write(0x1800, 1, 32, 0x200 + this->node_id); // setup cobid
    // this->sdo_write(0x1800, 2, 8, 0xFF);                   // send every sync
    // this->sdo_write(0x1800, 5, 16, 1);                     // enable RPDO
    // // setup RPO1
    // this->sdo_write(0x1801, 1, 32, 0x80000000);
    // this->sdo_write(0x1A01, 0, 8, 0);
    // this->sdo_write(0x1A01, 1, 32, 0x60640020); // actuel position
    // this->sdo_write(0x1A01, 2, 32, 0x606C0020); // actuel velocity
    // this->sdo_write(0x1A01, 0, 8, 2);
    // this->sdo_write(0x1801, 1, 32, 0x201 + this->node_id);
    // this->sdo_write(0x1801, 2, 8, 254);
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
        this->sdo_write(0x4004, 1, 8, 1);
    } else if (method_name == "disable") {
        Module::expect(arguments, 0);
        this->sdo_write(0x4004, 1, 8, 0);
    } else if (method_name == "speed") {
        Module::expect(arguments, 1, numbery);
        this->speed(arguments[0]->evaluate_number());
    } else if (method_name == "setup") {
        Module::expect(arguments, 0);
        this->setup();
    } else if (method_name == "homing") {
        Module::expect(arguments, 0);
        this->homing();
    } else if (method_name == "ppMode") {
        Module::expect(arguments, 1, integer);
        this->ppMode(arguments[0]->evaluate_integer());
    } else if (method_name == "speedMode") {
        Module::expect(arguments, 1, integer);
        this->speedMode(arguments[0]->evaluate_integer());
    } else if (method_name == "nmt_write") {
        Module::expect(arguments, 1, integer);
        this->nmt_write(arguments[0]->evaluate_integer());
    } else if (method_name == "reset") {
        Module::expect(arguments, 0);
        this->sdo_write(0x6040, 0, 16, 143);
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
    }
    if (id == 0x180 + this->node_id) {
        const uint16_t motor_speed = demarshal_unsigned<uint16_t>(data);
        this->properties["statusword"]->number_value = motor_speed;
        //     this->properties.at("statusword")->number_value = demarshal_unsigned<uint16_t>(data);
        //     this->properties.at("status_flag")->number_value = demarshal_unsigned<uint32_t>(data + 2);
    }
    if (id == 0x201 + this->node_id) {
        this->properties["position"]->number_value = demarshal_i32(data);
        this->properties["velocity"]->number_value = demarshal_i32(data + 4);
    }
}

void DunkerMotor::speed(const double speed) {
    const int32_t motor_speed = speed /
                                this->properties.at("m_per_tick")->number_value /
                                (this->properties.at("reversed")->boolean_value ? -1 : 1);
    this->sdo_write(0x4300, 1, 32, motor_speed, false);
}

double DunkerMotor::get_speed() {
    return this->properties.at("speed")->number_value;
}

void DunkerMotor::setup() {
    this->sdo_write(0x6040, 0, 16, 6);
    this->sdo_write(0x6040, 0, 16, 7);
    this->sdo_write(0x6040, 0, 16, 15);
}

void DunkerMotor::homing() {
    this->sdo_write(0x6060, 0, 8, 6);
    this->sdo_write(0x6092, 1, 32, this->properties["shaft_federate"]->number_value);
    this->sdo_write(0x6092, 2, 32, this->properties["shaft_revolutions"]->number_value);
    this->sdo_write(0x6099, 1, 32, this->properties["switch_search_speed"]->number_value);
    this->sdo_write(0x6099, 2, 32, this->properties["zero_search_speed"]->number_value);
    this->sdo_write(0x609A, 0, 32, this->properties["homing_acceleration"]->number_value);
    this->sdo_write(0x6098, 0, 8, this->properties["homing_method"]->number_value);
    this->sdo_write(0x607C, 0, 32, this->properties["homing_offset"]->number_value);
    this->sdo_write(0x6040, 0, 16, 15);
    this->sdo_write(0x6040, 0, 16, 0x1F); // TODO what code to write here
}

void DunkerMotor::ppMode(int32_t position) {
    // set mode to profile position mode
    this->sdo_write(0x6060, 0, 8, 1);
    // commit target position
    this->sdo_write(0x607A, 0, 32, position);
    // commit acceleration
    this->sdo_write(0x6083, 0, 32, this->properties["acceleration"]->number_value);
    // reset controlword
    this->sdo_write(0x6040, 0, 16, 15);
    // start motion
    this->sdo_write(0x6040, 0, 16, 0x1F);
}

void DunkerMotor::speedMode(int32_t speed) {
    // set mode to profile velocity mode
    this->sdo_write(0x6060, 0, 8, 3);
    // commit target velocity
    this->sdo_write(0x60FF, 0, 32, speed);
    // commit acceleration
    this->sdo_write(0x6083, 0, 32, this->properties["acceleration"]->number_value);
    // reset controlword
    this->sdo_write(0x6040, 0, 16, 15);
    // start motion
    this->sdo_write(0x6040, 0, 16, 0x1F);
}
