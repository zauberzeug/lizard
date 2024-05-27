#include "canopen_master.h"
#include "uart.h"
#include <cassert>
#include <esp_timer.h>

static constexpr char PROP_SYNC_INTERVAL[]{"sync_interval"};

CanOpenMaster::CanOpenMaster(const std::string &name, const Can_ptr can)
    : Module(canopen_master, name), can(can) {
    this->properties[PROP_SYNC_INTERVAL] = std::make_shared<IntegerVariable>(0);
}

void CanOpenMaster::send_sync() {
    this->can->send(0x80, nullptr, false, 0);
}

void CanOpenMaster::step() {
    int64_t sync_interval = this->properties[PROP_SYNC_INTERVAL]->integer_value;
    if (sync_interval == 0) {
        return;
    }

    sync_interval_counter++;

    if (sync_interval_counter >= sync_interval) {
        sync_interval_counter = 0;
        send_sync();
    }
}
