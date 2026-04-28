#pragma once

#include "can.h"
#include "module.h"
#include <memory>
#include <string>

class InnotronicMotorBase;
using InnotronicMotorBase_ptr = std::shared_ptr<InnotronicMotorBase>;

class InnotronicMotorBase : public Module, public std::enable_shared_from_this<InnotronicMotorBase> {
protected:
    const uint32_t node_id;
    const Can_ptr can;
    bool last_applied_enabled = true;

    bool is_enabled() const;
    bool is_debug() const;

    void send_switch_state(uint8_t state);
    void handle_status_msg(const uint8_t *data);

public:
    InnotronicMotorBase(const ModuleType type, const std::string name, const Can_ptr can, const uint32_t node_id);

    void configure(uint8_t setting_id, uint16_t value1, int32_t value2);
    void configure_node_id(uint8_t new_node_id);

    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void step() override;

    virtual void enable();
    virtual void disable();

    static const std::map<std::string, Variable_ptr> common_defaults();
};
