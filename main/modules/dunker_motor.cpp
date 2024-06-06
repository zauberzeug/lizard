#include "dunker_motor.h"
#include "canopen.h"
#include "uart.h"
#include <cinttypes>

DunkerMotor::DunkerMotor(const std::string &name, Can_ptr can, int64_t node_id)
    : Module(dunker_motor, name), can(can), node_id(check_node_id(node_id)) {
}

void DunkerMotor::subscribe_to_can() {
    can->subscribe(wrap_cob_id(COB_SDO_SERVER2CLIENT, node_id), this->shared_from_this());
}

void DunkerMotor::sdo_read(const uint16_t index, const uint8_t sub) {
    uint8_t data[8];
    data[0] = 0x40;
    data[1] = (index >> 0) & 0xFF;
    data[2] = (index >> 8) & 0xFF;
    data[3] = sub;
    this->can->send(0x600 + this->node_id, data);
}

void DunkerMotor::sdo_write(const uint16_t index, const uint8_t sub, const uint8_t bits, const uint32_t value) {
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
    } else if (method_name == "speed") {
        Module::expect(arguments, 1, numbery);
        this->sdo_write(0x4300, 1, 32, arguments[0]->evaluate_number());
    } else {
        Module::call(method_name, arguments);
    }
}

void DunkerMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    echo("DunkerMotor: Received CAN message with id: 0x%08" PRIX32 " and %d bytes of data\n", id, count);
}
