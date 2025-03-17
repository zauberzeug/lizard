#include "d1_motor.h"
#include "canopen.h"
#include "uart.h"
#include "utils/timing.h"
#include <cinttypes>

REGISTER_MODULE_DEFAULTS(D1Motor)

const std::map<std::string, Variable_ptr> D1Motor::get_defaults() {
    return {
        {"switch_search_speed", std::make_shared<IntegerVariable>()},
        {"zero_search_speed", std::make_shared<IntegerVariable>()},
        {"homing_acceleration", std::make_shared<IntegerVariable>()},
        {"profile_acceleration", std::make_shared<IntegerVariable>()},
        {"profile_velocity", std::make_shared<IntegerVariable>()},
        {"profile_deceleration", std::make_shared<IntegerVariable>()},
        {"position", std::make_shared<IntegerVariable>()},
        {"velocity", std::make_shared<IntegerVariable>()},
        {"status_word", std::make_shared<IntegerVariable>(-1)},
        {"status_flags", std::make_shared<IntegerVariable>()},
    };
}

D1Motor::D1Motor(const std::string &name, Can_ptr can, int64_t node_id)
    : Module(d1_motor, name), can(can), node_id(check_node_id(node_id)) {
    this->properties = D1Motor::get_defaults();
}

void D1Motor::subscribe_to_can() {
    can->subscribe(0x700 + node_id, this->shared_from_this()); // NMT response
    can->subscribe(0x580 + node_id, this->shared_from_this()); // SDO response
}

void D1Motor::sdo_read(const uint16_t index, const uint8_t sub) {
    uint8_t data[8];
    data[0] = 0x40;
    data[1] = (index >> 0) & 0xFF;
    data[2] = (index >> 8) & 0xFF;
    data[3] = sub;
    this->can->send(0x600 + this->node_id, data);
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

void D1Motor::nmt_write(const uint8_t cs) {
    uint8_t data[8];
    data[0] = cs;
    data[1] = this->node_id;
    this->can->send(0x000, data);
    this->wait();
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
    if (method_name == "setup") {
        Module::expect(arguments, 0);
        this->setup();
    } else if (method_name == "home") {
        Module::expect(arguments, 0);
        this->home();
    } else if (method_name == "profile_position") {
        Module::expect(arguments, 1, integer);
        this->profile_position(arguments[0]->evaluate_integer());
    } else if (method_name == "profile_velocity") {
        Module::expect(arguments, 1, integer);
        this->profile_velocity(arguments[0]->evaluate_integer());
    } else if (method_name == "stop") {
        Module::expect(arguments, 0);
        this->stop();
    } else if (method_name == "reset") {
        Module::expect(arguments, 0);
        this->sdo_write(0x6040, 0, 16, 143);
    } else if (method_name == "sdo_read") {
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
    } else if (method_name == "nmt_write") {
        Module::expect(arguments, 1, integer);
        this->nmt_write(arguments[0]->evaluate_integer());
    } else {
        throw std::runtime_error("Method " + method_name + " not found");
    }
}

void D1Motor::step() {
    this->sdo_read(0x6041, 0);
    this->sdo_read(0x2014, 0);
    this->sdo_read(0x6064, 0);
    this->sdo_read(0x606C, 0);
    Module::step();
}

void D1Motor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    if (id == 0x700 + this->node_id) {
        this->waiting_nmt_writes--;
    } else if (id == 0x580 + this->node_id) {
        this->waiting_sdo_writes--;
        if (data[1] == 0x41 && data[2] == 0x60) {
            this->properties["status_word"]->integer_value = data[5] << 8 | data[4];
        }
        if (data[1] == 0x14 && data[2] == 0x20) {
            this->properties["status_flags"]->integer_value = data[4];
        }
        if (data[1] == 0x64 && data[2] == 0x60) {
            this->properties["position"]->integer_value = data[5] << 8 | data[4];
        }
        if (data[1] == 0x6C && data[2] == 0x60) {
            this->properties["velocity"]->integer_value = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
        }
    }
}

void D1Motor::setup() {
    this->sdo_write(0x6040, 0, 16, 6);
    this->sdo_write(0x6040, 0, 16, 7);
    this->sdo_write(0x6040, 0, 16, 15);
}

void D1Motor::home() {
    this->sdo_write(0x6060, 0, 8, 6);
    // set specific homing parameters
    this->sdo_write(0x6099, 1, 32, this->properties["switch_search_speed"]->integer_value);
    this->sdo_write(0x6099, 2, 32, this->properties["zero_search_speed"]->integer_value);
    this->sdo_write(0x609A, 0, 32, this->properties["homing_acceleration"]->integer_value);
    this->sdo_write(0x6040, 0, 16, 15);
    this->sdo_write(0x6040, 0, 16, 0x1F);
}

void D1Motor::profile_position(const int32_t position) {
    // set mode to profile position mode
    this->sdo_write(0x6060, 0, 8, 1);
    // commit target position
    this->sdo_write(0x607A, 0, 32, position);
    // set driving parameters
    this->sdo_write(0x6083, 0, 32, this->properties["profile_acceleration"]->integer_value);
    this->sdo_write(0x6081, 0, 32, this->properties["profile_velocity"]->integer_value);
    this->sdo_write(0x6084, 0, 32, this->properties["profile_deceleration"]->integer_value);
    // reset control word
    this->sdo_write(0x6040, 0, 16, 15);
    // start motion
    this->sdo_write(0x6040, 0, 16, 0x1F);
}

void D1Motor::profile_velocity(const int32_t velocity) {
    // set mode to profile velocity mode
    this->sdo_write(0x6060, 0, 8, 3);
    // commit target velocity
    this->sdo_write(0x60FF, 0, 32, velocity);
    this->sdo_write(0x6083, 0, 32, this->properties["profile_acceleration"]->integer_value);
    // reset control word
    this->sdo_write(0x6040, 0, 16, 15);
    // start motion
    this->sdo_write(0x6040, 0, 16, 0x1F);
}

void D1Motor::stop() {
    this->sdo_write(0x6040, 0, 16, 7);
}
