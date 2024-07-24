#include "D1.h"
#include "canopen.h"
#include "uart.h"
#include "utils/timing.h"
#include <cinttypes>

D1Motor ::D1Motor(const std::string &name, Can_ptr can, int64_t node_id)
    : Module(d1_motor, name), can(can), node_id(check_node_id(node_id)) {
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

void D1Motor::subscribe_to_can() {
    can->subscribe(0x700 + node_id, this->shared_from_this()); // NMT response
    can->subscribe(0x580 + node_id, this->shared_from_this()); // SDO response
    can->subscribe(0x180 + node_id, this->shared_from_this()); // RPDO1
    can->subscribe(0x201 + node_id, this->shared_from_this()); // RPDO2

    // // setup RPDO
    this->sdo_write(0x1800, 1, 32, 0xC00001E0);
    this->sdo_write(0x1A00, 0, 8, 0);
    this->sdo_write(0x1A00, 1, 32, (0x6041 << 16) | (0 << 8) | 16); // statusword
    this->sdo_write(0x1A00, 2, 32, (0x2014 << 16) | (0 << 8) | 32); // status flag(referenced)
    this->sdo_write(0x1A00, 0, 8, 2);
    this->sdo_write(0x1800, 1, 32, 0x180 + this->node_id);
    // // setup RPO1
    // this->sdo_write(0x1801, 1, 32, -1);
    // this->sdo_write(0x1A01, 0, 8, 0);
    // this->sdo_write(0x1A01, 1, 32, (0x6064 << 16) | (0 << 8) | 32); // actuel position
    // this->sdo_write(0x1A01, 2, 32, (0x606C << 16) | (0 << 8) | 32); // actuel velocity
    // this->sdo_write(0x1A01, 0, 8, 2);
    // this->sdo_write(0x1801, 1, 32, 0x201 + this->node_id);
}

void D1Motor::sdo_read(const uint16_t index, const uint8_t sub) {
    uint8_t data[8];
    data[0] = 0x40;
    data[1] = (index >> 0) & 0xFF;
    data[2] = (index >> 8) & 0xFF;
    data[3] = sub;
    this->can->send(0x600 + this->node_id, data);
}

void D1Motor::nmt_write(const uint8_t cs) {
    uint8_t data[8];
    data[0] = cs;
    data[1] = this->node_id;
    this->can->send(0x000, data);
    this->wait();
}

void D1Motor::sdo_write(const uint16_t index, const uint8_t sub, const uint8_t bits, const uint32_t value, const bool wait) {
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

void D1Motor::wait() {

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

void D1Motor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
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
        throw std::runtime_error("Method " + method_name + " not found");
    }
}

void D1Motor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    echo("message received");
    if (id == 0x700 + this->node_id) {
        this->waiting_nmt_writes--;
    } else if (id == 0x580 + this->node_id) {
        this->waiting_sdo_writes--;
    } else if (id == 0x180 + this->node_id) {
        echo("message is there");
        this->properties["statusword"]->number_value = demarshal_unsigned<uint16_t>(data);
        this->properties["status_flag"]->number_value = demarshal_unsigned<uint32_t>(data + 2);
    } else if (id == 0x201 + this->node_id) {
        this->properties["position"]->number_value = demarshal_i32(data);
        this->properties["velocity"]->number_value = demarshal_i32(data + 4);
    }
}

void D1Motor::setup() {
    this->sdo_write(0x6040, 0, 16, 6);
    this->sdo_write(0x6040, 0, 16, 7);
    this->sdo_write(0x6040, 0, 16, 15);
}

void D1Motor::homing() {
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

// void D1Motor::setup() {
//     if (this->properties["statusword"]->number_value == 664) {
//         this->sdo_write(0x6040, 0, 16, 6);
//         this->setup();
//     } else if (this->properties["statusword"]->number_value == 633) {
//         this->sdo_write(0x6040, 0, 16, 7);
//         this->setup();
//     } else if (this->properties["statusword"]->number_value == 635) {
//         this->sdo_write(0x6040, 0, 16, 15);
//         this->setup();
//     } else if (this->properties["statusword"]->number_value == 639) {
//         return;
//     } else {
//         throw std::runtime_error(std::string("D1 setup failed"));
//     }
// }

void D1Motor::ppMode(int32_t position) {
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

void D1Motor::speedMode(int32_t speed) {
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
