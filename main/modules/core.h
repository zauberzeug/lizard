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
    bool stored = false;          // a bus slot holds a frame of this id
    uint32_t last_poll_count = 0; // frame mode 1: the poll that triggered the last frame
};

class Core;
using Core_ptr = std::shared_ptr<Core>;

class Core : public Module {
private:
    std::list<struct output_element_t> output_list;
    mutable bool output_overflow_reported = false;
    unsigned long int last_message_millis = 0;

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
    // experiment: microseconds the last step spent in module steps and in rules plus routines
    void record_step_timing(int64_t modules_us, int64_t rules_us);
    void record_parse(int64_t bytes);
    // builds the frames that are due; runs at the end of every main loop iteration
    void emit_telemetry();
    // frame modes 2 and 3: the bus communication task builds the frames due for `requester` while answering its poll,
    // with the interpreter lock (2) or without it (3, to show the race)
    void build_frames_for_poll(SerialBus *bus, uint8_t requester, bool locked = true);
};
