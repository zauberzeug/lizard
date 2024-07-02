#include "D1.h"
#include "canopen.h"

D1Motor ::D1Motor(const std::string &name, Can_ptr can, int64_t node_id)
    : Module(d1_motor, name), can(can), node_id(check_node_id(node_id)) {
}

void D1Motor::subscribe_to_can() {
    can->subscribe(0x700 + node_id, this->shared_from_this()); // NMT response
    can->subscribe(0x580 + node_id, this->shared_from_this()); // SDO response
    can->subscribe(0x200 + node_id, this->shared_from_this()); // RPDO1

    // restart device
    this->nmt_write(0x81); // TODO: check if this is correct

    // TODO setup RPDO

    // enter operational state
    this->nmt_write(0x01); // TODO: check if this is correct
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
    // TODO write calls
    else {
        throw std::runtime_error("Method " + method_name + " not found");
    }
}

void D1Motor::handle_can_msg(const uint32_t id, const int count, const uint8_t *const data) {
    if (id == 0x700 + this->node_id) {
        this->waiting_nmt_writes--;
    } else if (id == 0x580 + this->node_id) {
        this->waiting_sdo_writes--;
    } else if (id == 0x200 + this->node_id) {
        // TODO handle RPDO
    }
