#pragma once

#include "../utils/telemetry.h"
#include "module.h"
#include "serial_bus.h"
#include <map>
#include <string>
#include <vector>

// The core's view of one bus peer: declared properties that mirror the peer's values, ordered from the peer as
// telemetry frames and copied from each arriving frame without parsing.
class BusTelemetry : public Module {
private:
    std::vector<const std::string *> declared;         // keys of the property map, in declaration order
    std::map<uint8_t, std::vector<Variable *>> mapped; // per frame: the mirror of each field index, nullptr if none
    std::map<uint8_t, std::vector<telemetry::Slot>> slots;
    bool orders_sent = false;
    bool frame_seen = false;
    unsigned long last_frame_millis = 0;
    Variable_ptr age;
    Variable_ptr peer_millis;
    Variable_ptr frames;

    void send_order(const std::vector<const std::string *> &names);

public:
    static inline constexpr const char *TYPE = "BusTelemetry";

    const SerialBus_ptr bus;
    const uint8_t peer_id;
    const unsigned long interval;

    BusTelemetry(const std::string name, const SerialBus_ptr bus, uint8_t peer_id, unsigned long interval);
    ~BusTelemetry() override;
    void step() override;
    void declare_property(const std::string &property_name, const Variable_ptr &variable) override;
    void write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) override;
    bool declares(const std::string &name) const;
    void send_orders();
    void reset_layout();
    void handle_layout_line(const telemetry::LayoutLine &line, const telemetry::Layout &layout);
    bool handle_frame(uint8_t frame_id, uint8_t seq, uint32_t peer_millis, const uint8_t *payload);
    static const std::map<std::string, Variable_ptr> get_defaults();
};
