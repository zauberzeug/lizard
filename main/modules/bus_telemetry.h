#pragma once

#include "../utils/telemetry.h"
#include "module.h"
#include "serial_bus.h"
#include <map>
#include <string>
#include <string_view>
#include <vector>

// The coordinator's view of one serial bus peer: declared properties that mirror fields of the peer's telemetry frames.
// The peer defines its frames itself; this module only copies the declared fields out of every frame that arrives.
class BusTelemetry : public Module {
private:
    struct Mirror {
        const std::string *name; // key in the property map
        Variable *variable;
        bool received = false; // a frame delivered a value since the peer's last restart
        bool warned = false;   // that no frame carries it, once per run of the peer
        bool mismatch = false; // the peer sends it with another type
    };
    struct Field {
        uint16_t index;    // in the frame
        uint16_t mirror;   // in `mirrors`
        uint16_t position; // byte offset in the payload, for bools the bit after the numbers
    };
    struct FrameMap {
        std::vector<Field> fields; // only the mirrored ones
        bool positioned = false;   // the positions follow from the types of all fields, known with the first frame
        size_t bits_offset = 0;
    };

    std::vector<Mirror> mirrors;
    std::map<uint8_t, FrameMap> frames; // by frame ID, only frames with mirrored fields, dropped with each new layout of the ID
    bool frames_arriving = false;       // since the last declaration or restart of the peer
    unsigned long frames_since_millis = 0;
    bool missing_checked = false;
    unsigned long silence_since_millis = 0; // the last declaration or restart of the peer, while no frame arrived
    bool silence_warned = false;
    bool layout_unreadable = false; // the peer sends a layout version this node does not read, until its restart
    const std::string peer_label;   // names the peer in warnings

    int find_mirror(const std::string_view name) const;
    bool is_mapped(const size_t mirror) const;
    void restart_missing_check();

public:
    static inline constexpr const char *TYPE = "BusTelemetry";

    const SerialBus_ptr bus;
    const uint8_t peer_id;

    // warnings name the peer "peer <id>" unless `peer_label` names it otherwise, e.g. "the expander"
    BusTelemetry(const std::string &name, const SerialBus_ptr bus, const uint8_t peer_id, const std::string &peer_label = "");
    ~BusTelemetry() override;
    void step() override;
    void write_property(const std::string property_name, const ConstExpression_ptr expression, const bool from_expander) override;
    void declare_property(const std::string &property_name, const Variable_ptr &variable) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

    // called by the bus on the main task
    bool declares(const std::string &property_name) const { return this->properties.count(property_name) > 0; }
    void handle_layout_line(const telemetry::LayoutLine &line, const bool redefined);
    void handle_frame(const uint8_t *body, const std::vector<char> *types); // without types while the bus lacks the frame's layout
    void mark_layout_unreadable();                                          // the peer's layout lines have another version
    void reset();                                                           // the peer restarted and announces its layouts anew
};
