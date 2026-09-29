#pragma once

#include "../utils/telemetry.h"
#include "module.h"
#include <deque>
#include <memory>
#include <utility>

struct output_element_t {
    const ConstModule_ptr module;
    const std::string property_name;
    const unsigned int precision;
};

class SerialBus;

struct TelemetryFrame {
    uint8_t id;
    std::vector<telemetry::Field> fields;
    unsigned long interval; // milliseconds, 0: every step (every poll on a bus peer)
    SerialBus *bus;         // the bus the frame was ordered on, nullptr for the node's own frame
    uint8_t destination;    // the node that ordered it, 0 for the node's own frame
    unsigned long last_millis = 0;
    uint8_t seq = 0;
    bool stored = false; // a bus slot holds a frame of this id
};

class Core;
using Core_ptr = std::shared_ptr<Core>;

class Core : public Module {
private:
    std::list<struct output_element_t> output_list;
    mutable bool output_overflow_reported = false;
    unsigned long int last_message_millis = 0;

    Variable_ptr telemetry_info_rate; // resolved once: a lookup by this name would allocate a temporary string every step
    std::vector<TelemetryFrame> telemetry_frames;
    std::deque<std::pair<uint8_t, size_t>> pending_layout; // frame id and field index still to announce
    void define_telemetry(const std::vector<ConstExpression_ptr> &arguments);
    void clear_telemetry();
    SerialBus *polled_bus() const;
    bool route(const TelemetryFrame &frame, SerialBus *polled, SerialBus *&bus, uint8_t &destination) const;
    void send_layout(const TelemetryFrame &frame, size_t index);
    void announce(const TelemetryFrame &frame);
    size_t encode_frame(TelemetryFrame &frame, unsigned long now, char *line, size_t capacity);

public:
    Core(const std::string name);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    double get(const std::string property_name) const;
    void set(std::string property_name, double value);
    std::string get_output() const override;
    void keep_alive();
    // builds the frames that are due; runs at the end of every main loop iteration
    void emit_telemetry();
};
