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

void DunkerMotor::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "sdo_read") {
        if (arguments.size() < 1 || arguments.size() > 2) {
            throw std::runtime_error("unexpected number of arguments");
        }
        expect(arguments, -1, integer, integer);
        uint16_t index = arguments[0]->evaluate_integer();
        uint8_t sub = arguments.size() >= 2 ? arguments[1]->evaluate_integer() : 0;
        uint8_t data[8];
        data[0] = 0x40; // Read 32-bit expedited
        data[1] = (index >> 0) & 0xFF;
        data[2] = (index >> 8) & 0xFF;
        data[3] = sub;
        this->can->send(0x600 + node_id, data);
    } else if (method_name == "sdo_write") {
        expect(arguments, 3, integer, integer, integer);
        uint16_t index = arguments[0]->evaluate_integer();
        uint8_t sub = arguments[1]->evaluate_integer();
        uint32_t value = arguments[2]->evaluate_integer();
        uint8_t data[8];
        data[0] = 0x23; // Write 32-bit expedited
        data[1] = (index >> 0) & 0xFF;
        data[2] = (index >> 8) & 0xFF;
        data[3] = sub;
        data[4] = (value >> 0) & 0xFF;
        data[5] = (value >> 8) & 0xFF;
        data[6] = (value >> 16) & 0xFF;
        data[7] = (value >> 24) & 0xFF;
        can->send(0x600 + node_id, data);
    } else {
        Module::call(method_name, arguments);
    }
}

void DunkerMotor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    echo("DunkerMotor: Received CAN message with id: 0x%08" PRIX32 " and %d bytes of data\n", id, count);
}
