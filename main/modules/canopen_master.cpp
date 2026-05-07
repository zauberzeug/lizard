#include "canopen_master.h"
#include "module_helpers.h"

static Module_ptr create_canopen_master(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    Module::expect(arguments, 1, identifier);
    const Can_ptr can = get_module_argument<Can>(arguments[0], "Can");
    return std::make_shared<CanOpenMaster>(name, can);
}
REGISTER_MODULE(CanOpenMaster, &create_canopen_master)

const std::map<std::string, Variable_ptr> CanOpenMaster::get_defaults() {
    return {
        {"sync_interval", std::make_shared<IntegerVariable>(0)},
    };
}

CanOpenMaster::CanOpenMaster(const std::string &name, const Can_ptr can)
    : Module("CanOpenMaster", name), can(can) {
    this->properties = CanOpenMaster::get_defaults();
}

void CanOpenMaster::step() {
    const int64_t sync_interval = this->properties["sync_interval"]->integer_value;
    this->sync_interval_counter++;

    if (sync_interval > 0 && this->sync_interval_counter >= sync_interval) {
        this->sync_interval_counter = 0;
        this->can->send(0x80, nullptr, false, 0);
    }
}
