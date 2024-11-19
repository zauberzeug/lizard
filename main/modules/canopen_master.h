#pragma once

#include "can.h"
#include "module.h"

class CanOpenMaster;
using CanOpenMaster_ptr = std::shared_ptr<CanOpenMaster>;

class CanOpenMaster : public Module, public std::enable_shared_from_this<CanOpenMaster> {
private:
    const Can_ptr can;
    int64_t sync_interval_counter = 0;

public:
    CanOpenMaster(const std::string &name, const Can_ptr can);
    void step() override;
    static const std::map<std::string, Variable_ptr> &get_defaults();
};
