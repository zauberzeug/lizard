#pragma once

#include "bus_telemetry.h"
#include "serial_bus.h"
#include <memory>

// A serial bus to a port expander wired like an Expander: this node polls the expander as its only peer, flashes and
// restarts it through its boot and enable pins, and mirrors the expander's telemetry frames in declared properties.
class ExpanderBus : public SerialBus {
private:
    std::unique_ptr<BusTelemetry> telemetry;

public:
    static inline constexpr const char *TYPE = "ExpanderBus";
    static constexpr uint8_t NODE_ID = 1;
    static constexpr uint8_t PEER_ID = 2;

    ExpanderBus(const std::string &name, const ConstSerial_ptr serial, const gpio_num_t boot_pin, const gpio_num_t enable_pin);
    ~ExpanderBus() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    void write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) override;
    void declare_property(const std::string &property_name, const Variable_ptr &variable) override;
};
