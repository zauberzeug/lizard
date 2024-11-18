#include "canopen_master.h"

std::map<std::string, Variable_ptr> CanOpenMaster::get_default_properties() const {
    return {
        {"sync_interval", std::make_shared<IntegerVariable>(0)}};
}

CanOpenMaster::CanOpenMaster(const std::string &name, const Can_ptr can)
    : Module(canopen_master, name), can(can) {
    this->properties = this->get_default_properties();
}

void CanOpenMaster::step() {
    const int64_t sync_interval = this->properties["sync_interval"]->integer_value;
    this->sync_interval_counter++;

    if (sync_interval > 0 && this->sync_interval_counter >= sync_interval) {
        this->sync_interval_counter = 0;
        this->can->send(0x80, nullptr, false, 0);
    }
}
