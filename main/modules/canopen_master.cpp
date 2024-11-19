#include "canopen_master.h"

CanOpenMaster::CanOpenMaster(const std::string &name, const Can_ptr can)
    : Module(canopen_master, name), can(can) {
    this->properties["sync_interval"] = std::make_shared<IntegerVariable>(0);
}

void CanOpenMaster::step() {
    const int64_t sync_interval = this->properties["sync_interval"]->integer_value;
    this->sync_interval_counter++;

    if (sync_interval > 0 && this->sync_interval_counter >= sync_interval) {
        this->sync_interval_counter = 0;
        this->can->send(0x80, nullptr, false, 0);
    }
}
