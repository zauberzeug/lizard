#include "serial_bus.h"

#include "../main.h"
#include "../utils/format.h"
#include "../utils/otb.h"
#include "../utils/string_utils.h"
#include "../utils/telemetry.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "module_helpers.h"
#include "serial.h"
#include <esp_timer.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

static constexpr size_t FRAME_BUFFER_SIZE = 512;
static constexpr unsigned long POLL_TIMEOUT_MS = 250;
static constexpr size_t OUTGOING_QUEUE_LENGTH = 32;
static constexpr size_t INCOMING_QUEUE_LENGTH = 32;
static constexpr size_t OFFSET_QUEUE_LENGTH = 8;            // full-state snapshots; a full queue only delays one
static_assert(OUTGOING_QUEUE_LENGTH > otb::BUS_OTB_WINDOW); // a stalled poll must not abort an OTB update
static_assert(INCOMING_QUEUE_LENGTH > otb::BUS_OTB_WINDOW); // a stalled step() must not drop an OTB chunk
static constexpr const char ECHO_CMD[] = "__ECHO__";
static constexpr const char POLL_CMD[] = "__POLL__";
static_assert(otb::BUS_OTB_CHUNK_LINE_SIZE < SerialBus::PAYLOAD_CAPACITY, "OTB chunk lines must fit the bus payload");
static constexpr const char DONE_CMD[] = "__DONE__";
static constexpr size_t MAX_STAMP_DIGITS = 18; // beyond this strtoll saturates and skews the offset
static constexpr size_t DONE_STAMP_FIELDS = 3; // T3, T3-T2, sequence number of the answered POLL
// places in the inbound queue that telemetry frames never take
static constexpr size_t INBOUND_QUEUE_RESERVE = otb::BUS_OTB_WINDOW;
static constexpr unsigned long LAYOUT_REQUEST_INTERVAL_MS = 2000;
static constexpr unsigned long SLOT_WARNING_INTERVAL_MS = 10000;
static constexpr const char LAYOUT_REQUEST[] = "core.telemetry_info()";

// Estimator tunables: an unlocked estimate takes the first sample; from then on a sample whose accuracy
// bound is at most SYNC_ACCEPT_ACCURACY_US replaces the estimate immediately, otherwise the best sample
// within SYNC_WINDOW_MS does, so that the estimate keeps following crystal drift (~20 ppm, i.e. ~20 us
// per second) even when the bus is busy. On F31 (RS485 at 460800) the bound sits at 0.4-0.9 ms, set by
// the 1 ms tick of the two communication tasks, so the threshold mainly decides how soon a sample is taken.
static constexpr int64_t SYNC_ACCEPT_ACCURACY_US = 500;
static constexpr unsigned long SYNC_WINDOW_MS = 2000;

static Module_ptr create_serial_bus(const std::string &name, const std::vector<ConstExpression_ptr> &arguments, MessageHandler) {
    Module::expect(arguments, 2, identifier, integer);
    const ConstSerial_ptr serial = get_module_argument<const Serial>(arguments[0]);
    const long node_id = arguments[1]->evaluate_integer();
    if (node_id <= 0 || node_id >= 255) {
        throw std::runtime_error("node ID must be between 0 and 255");
    }
    return std::make_shared<SerialBus>(name, serial, node_id);
}
REGISTER_MODULE(SerialBus, &create_serial_bus)

static uint8_t to_peer_id(const ConstExpression_ptr &argument) {
    if ((argument->type & integer) == 0) {
        throw std::runtime_error("peer IDs must be integers");
    }
    const long peer_value = argument->evaluate_integer();
    if (peer_value <= 0 || peer_value >= 255) {
        throw std::runtime_error("peer IDs must be between 0 and 255");
    }
    return static_cast<uint8_t>(peer_value);
}

const std::map<std::string, Variable_ptr> SerialBus::get_defaults() {
    return {
        {"telemetry_errors", std::make_shared<IntegerVariable>(0)},
        {"telemetry_dropped", std::make_shared<IntegerVariable>(0)},
    };
}

SerialBus::SerialBus(const std::string &name, const ConstSerial_ptr serial, const uint8_t node_id)
    : Module(name), serial(serial), node_id(node_id) {
    this->properties = SerialBus::get_defaults();
    this->telemetry_errors = this->properties.at("telemetry_errors");
    this->telemetry_dropped = this->properties.at("telemetry_dropped");
    this->serial->enable_line_detection();

    // everything that can throw comes before the task exists: an exception from a constructor unwinds without running
    // the destructor, so a task created earlier would keep dereferencing a destroyed bus
    this->echo_callback_handle = register_echo_callback([this](const char *line) { this->handle_echo(line); });
    this->otb_session.bus_name = this->name.c_str();
    this->otb_session.send_fn = [this](uint8_t receiver, const char *data, size_t len) {
        this->enqueue_outgoing_message(receiver, data, len);
    };

    this->config_queue = xQueueCreate(1, sizeof(Config));
    this->offset_queue = xQueueCreate(OFFSET_QUEUE_LENGTH, sizeof(OffsetUpdate));
    this->outbound_queue = xQueueCreate(OUTGOING_QUEUE_LENGTH, sizeof(OutgoingMessage));
    this->inbound_queue = xQueueCreate(INCOMING_QUEUE_LENGTH, sizeof(IncomingMessage));
    const bool queues_created = this->config_queue && this->offset_queue && this->outbound_queue && this->inbound_queue;
    if (!queues_created ||
        xTaskCreatePinnedToCore(
            SerialBus::communication_loop, "serial_bus_comm", 4096, this, 5, &this->communication_task, 1) != pdPASS) {
        for (const QueueHandle_t queue :
             {this->config_queue, this->offset_queue, this->outbound_queue, this->inbound_queue}) {
            if (queue) {
                vQueueDelete(queue);
            }
        }
        unregister_echo_callback(this->echo_callback_handle);
        throw std::runtime_error(queues_created ? "failed to create serial bus communication task"
                                                : "failed to create serial bus queues");
    }
    // claim last: a claim is never released, so it must not outlive a constructor that throws
    this->serial->claim(name);
    telemetry::add_bus(this);
}

// only reached when a constructed bus is dropped again, e.g. because Global::add_module() throws; the task must be gone
// before the queues and `this` are
SerialBus::~SerialBus() {
    telemetry::remove_bus(this);
    unregister_echo_callback(this->echo_callback_handle);
    this->stop_requested = true;
    for (int i = 0; i < 100 && !this->stopped; ++i) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    // the task parks itself once it has left its loop, so this is the only delete and cannot collide with a self-delete
    vTaskDelete(this->communication_task);
    for (const QueueHandle_t queue : {this->config_queue, this->offset_queue, this->outbound_queue, this->inbound_queue}) {
        vQueueDelete(queue);
    }
    delete[] this->frame_slots.load();
}

void SerialBus::step() {
    OffsetUpdate update;
    while (xQueueReceive(this->offset_queue, &update, 0) == pdTRUE) {
        this->apply_offset_update(update);
    }

    IncomingMessage message;
    while (xQueueReceive(this->inbound_queue, &message, 0) == pdTRUE) {
        this->handle_incoming_message(message);
    }

    // the communication task must not echo() itself, so drops are counted there and reported here, at most once per second
    if (this->dropped_inbound > 0 && millis_since(this->last_drop_report_millis) > 1000) {
        const unsigned dropped = this->dropped_inbound.exchange(0);
        this->last_drop_report_millis = millis();
        echo("warning: serial bus %s dropped %u inbound messages (queue full)", this->name.c_str(), dropped);
    }
    if (this->dropped_frames > 0) {
        this->telemetry_dropped->set_integer_value(this->telemetry_dropped->integer_value() + this->dropped_frames.exchange(0));
    }

    if (this->otb_session.handle != 0) {
        otb::bus_tick(this->otb_session);
    }

    Module::step();
}

void SerialBus::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "send") {
        // bus.send(receiver, fmt[, args...]) — printf-style formatting.
        // See utils/format.h for supported specifiers.
        if (arguments.size() < 2) {
            throw std::runtime_error("send expects at least 2 arguments (receiver, format[, args...])");
        }
        if ((arguments[0]->type & integer) == 0) {
            throw std::runtime_error("receiver ID must be an integer");
        }
        if ((arguments[1]->type & string) == 0) {
            throw std::runtime_error("format must be a string");
        }
        const int receiver = arguments[0]->evaluate_integer();
        if (receiver <= 0 || receiver >= 255) {
            throw std::runtime_error("receiver ID must be between 0 and 255");
        }
        const std::string payload = format_args(arguments[1]->evaluate_string(), arguments, 2);
        this->enqueue_outgoing_message(static_cast<uint8_t>(receiver), payload.c_str(), payload.size());
    } else if (method_name == "make_coordinator") {
        if (arguments.empty()) {
            throw std::runtime_error("make_coordinator expects at least one peer ID");
        }
        if (arguments.size() > sizeof(Config::peer_ids)) {
            throw std::runtime_error("too many peer IDs");
        }
        uint8_t peer_ids[sizeof(Config::peer_ids)];
        uint8_t peer_count = 0;
        for (const auto &argument : arguments) {
            peer_ids[peer_count++] = to_peer_id(argument);
        }
        std::memcpy(this->config.peer_ids, peer_ids, peer_count);
        this->config.peer_count = peer_count;
        this->send_config();
    } else if (method_name == "enable_time_sync") {
        Module::expect(arguments, 0);
        this->config.time_sync_enabled = true;
        this->send_config();
    } else if (method_name == "pass_telemetry") {
        std::vector<uint8_t> peer_ids;
        for (const auto &argument : arguments) {
            peer_ids.push_back(to_peer_id(argument));
        }
        this->pass_all_telemetry = peer_ids.empty();
        this->passed_peers = std::move(peer_ids);
        // a peer that is passed on anew is asked for its layout lines with its first frame
        for (auto it = this->peer_layouts.begin(); it != this->peer_layouts.end();) {
            it = this->passes_telemetry(it->first) ? std::next(it) : this->peer_layouts.erase(it);
        }
    } else {
        Module::call(method_name, arguments);
    }
}

void SerialBus::send_config() {
    // a length-1 queue with overwrite semantics: the communication task only ever sees the latest config
    ++this->config.generation;
    xQueueOverwrite(this->config_queue, &this->config);
    this->rebuild_offset_properties();
}

void SerialBus::rebuild_offset_properties() {
    if (!this->config.time_sync_enabled) {
        return;
    }
    // Every estimate restarts with the new config (relocking takes a single good sample), so all
    // existing offset properties turn NaN, including those of peers dropped from the list, which
    // keep reading NaN instead of freezing their last value.
    for (auto &[name, property] : this->properties) {
        if (starts_with(name, "offset_")) {
            property->set_number_value(NAN);
        }
    }
    const auto find_or_create = [this](const std::string &name) {
        Variable_ptr &property = this->properties[name];
        if (!property) {
            property = std::make_shared<NumberVariable>(NAN);
        }
        return property;
    };
    this->offset_properties.clear();
    for (size_t i = 0; i < this->config.peer_count; ++i) {
        const uint8_t peer_id = this->config.peer_ids[i];
        const std::string name = "offset_" + std::to_string(peer_id);
        this->offset_properties.push_back({peer_id, find_or_create(name), find_or_create(name + "_accuracy")});
    }
}

void SerialBus::apply_offset_update(const OffsetUpdate &update) {
    if (update.generation != this->config.generation) {
        return; // estimate from a superseded config; its properties were already reset
    }
    for (const OffsetProperties &properties : this->offset_properties) {
        if (properties.peer_id == update.peer_id) {
            properties.offset->set_number_value(update.valid ? update.offset_us / 1000.0 : NAN);
            properties.accuracy->set_number_value(update.valid ? update.accuracy_us / 1000.0 : NAN);
            return;
        }
    }
}

void SerialBus::communication_loop(void *param) {
    SerialBus *bus = static_cast<SerialBus *>(param);
    while (!bus->stop_requested) {
        if (xQueueReceive(bus->config_queue, &bus->received_config, 0) == pdTRUE) {
            bus->adopt_config(bus->received_config);
        }
        bus->publish_pending_peer_clocks();
        bus->process_uart();
        if (bus->is_coordinator()) {
            // poll next peer
            if (!bus->is_polling && !bus->send_outgoing_queue()) {
                bus->send_poll();
            }
            // handle poll timeout
            if (bus->is_polling && millis_since(bus->poll_start_millis) > POLL_TIMEOUT_MS) {
                bus->print_to_incoming_queue("warning: serial bus %s poll to %u timed out",
                                             bus->name.c_str(), bus->polled_peer_id);
                bus->is_polling = false;
                if (bus->time_sync_enabled) {
                    bus->reset_peer_clock(bus->polled_peer_id);
                }
            }
        } else {
            // respond to poll
            if (bus->requesting_node) {
                try {
                    // if this is the first response, send "Ready." (will be simplified in the future when broadcasts are implemented)
                    if (bus->ready_pending) {
                        char payload[PAYLOAD_CAPACITY];
                        const int len = std::snprintf(payload, sizeof(payload), "%sReady.", ECHO_CMD);
                        bus->send_message(bus->requesting_node, payload, len);
                        bus->ready_pending = false;
                    }
                    bus->send_outgoing_queue();
                    bus->send_frame_slots(bus->requesting_node);
                    char done[sizeof(DONE_CMD) + DONE_STAMP_FIELDS * (MAX_STAMP_DIGITS + 1)];
                    int done_len = std::snprintf(done, sizeof(done), "%s", DONE_CMD);
                    if (bus->time_sync_enabled) {
                        // T3 is stamped as late as possible; the elapsed time since T2 lets the
                        // coordinator cancel everything that happened on this side in between
                        const int64_t t3 = esp_timer_get_time();
                        done_len += std::snprintf(done + done_len, sizeof(done) - done_len, "%lld,%lld,%lu",
                                                  (long long)t3, (long long)(t3 - bus->poll_received_us),
                                                  (unsigned long)bus->poll_received_seq);
                    }
                    bus->send_message(bus->requesting_node, done, done_len);
                } catch (const std::exception &e) {
                    bus->print_to_incoming_queue("warning: serial bus %s error while responding to poll: %s", bus->name.c_str(), e.what());
                }
                bus->requesting_node = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    bus->stopped = true;
    while (true) {
        vTaskSuspend(nullptr); // parked until the destructor deletes the task
    }
}

void SerialBus::adopt_config(const Config &config) {
    this->peer_ids.assign(config.peer_ids, config.peer_ids + config.peer_count);
    this->time_sync_enabled = config.time_sync_enabled;
    this->generation = config.generation;
    // every estimate starts fresh; a re-used ID must not inherit the previous board's state
    this->peer_clocks.clear();
    for (const uint8_t id : this->peer_ids) {
        PeerClock clock;
        clock.peer_id = id;
        this->peer_clocks.push_back(clock);
    }
    // restart polling with the first listed peer, but let an outstanding POLL finish first: a second POLL
    // would collide with the DONE that is still on its way and could be paired with the wrong T1
    this->poll_index = 0;
}

// Parses either an empty string (all fields -1) or exactly `count` comma-separated fields of 1 to
// MAX_STAMP_DIGITS digits each; returns false for anything else, which then counts as a user payload
// that merely shares the command prefix.
static bool parse_stamp(const char *stamp, int64_t *fields, const size_t count) {
    std::fill(fields, fields + count, -1);
    if (*stamp == '\0') {
        return true;
    }
    const char *c = stamp;
    for (size_t field = 0; field < count; ++field) {
        const char *start = c;
        while (std::isdigit(static_cast<unsigned char>(*c))) {
            ++c;
        }
        const size_t digits = c - start;
        if (digits == 0 || digits > MAX_STAMP_DIGITS) {
            return false;
        }
        fields[field] = std::strtoll(start, nullptr, 10);
        if (field + 1 < count && *c++ != ',') {
            return false;
        }
    }
    return *c == '\0';
}

void SerialBus::process_uart() {
    static char buffer[FRAME_BUFFER_SIZE];
    while (this->serial->has_buffered_lines()) {
        const int len = this->serial->read_line(buffer, sizeof(buffer));
        if (len < 0) {
            this->print_to_incoming_queue("warning: serial bus %s error while processing uart: %s", this->name.c_str(), Serial::read_line_error(len));
            continue;
        }
        if (bool ok; (check(buffer, len, &ok), !ok)) {
            this->print_to_incoming_queue("warning: serial bus %s checksum mismatch: %s", this->name.c_str(), buffer);
            continue;
        }

        // parse message
        IncomingMessage message;
        if (!this->parse_message(buffer, message)) {
            this->print_to_incoming_queue("warning: serial bus %s could not parse message: %s", this->name.c_str(), buffer);
            continue;
        }

        // ignore messages not for this node
        if (message.receiver != this->node_id) {
            continue;
        }

        // handle poll command, optionally carrying a sequence number to echo in the DONE
        if (std::strncmp(message.payload, POLL_CMD, sizeof(POLL_CMD) - 1) == 0) {
            int64_t seq;
            if (parse_stamp(message.payload + sizeof(POLL_CMD) - 1, &seq, 1) && seq <= UINT32_MAX) {
                this->poll_received_us = esp_timer_get_time(); // T2
                this->poll_received_seq = seq < 0 ? 0 : seq;
                this->requesting_node = message.sender;
                this->coordinator_id = message.sender;
                continue;
            }
        }

        // handle done command, optionally stamped with T3, T3-T2 and the sequence number of the answered POLL
        if (std::strncmp(message.payload, DONE_CMD, sizeof(DONE_CMD) - 1) == 0) {
            int64_t stamp[DONE_STAMP_FIELDS];
            if (parse_stamp(message.payload + sizeof(DONE_CMD) - 1, stamp, DONE_STAMP_FIELDS)) {
                // len is the raw frame length incl. newline, i.e. the bytes on the wire
                this->handle_done(message.sender, stamp, esp_timer_get_time(), len);
                continue;
            }
        }

        // telemetry frames never take the last slots of the inbound queue, so that a flood of them, e.g. while the main
        // task runs the startup, cannot crowd out commands, echoes and OTB chunks
        if (message.payload[0] == telemetry::FRAME_PREFIX && uxQueueSpacesAvailable(this->inbound_queue) <= INBOUND_QUEUE_RESERVE) {
            this->dropped_frames++;
            continue;
        }

        this->push_incoming(message);
    }
}

void SerialBus::push_incoming(const IncomingMessage &message) {
    // a warning could not pass the full queue either, so count the drop and let step() report it
    if (xQueueSend(this->inbound_queue, &message, 0) != pdTRUE) {
        this->dropped_inbound++;
    }
}

void SerialBus::send_poll() {
    this->polled_peer_id = this->peer_ids[this->poll_index];
    this->poll_index = (this->poll_index + 1) % this->peer_ids.size();
    char payload[sizeof(POLL_CMD) + MAX_STAMP_DIGITS];
    int length = std::snprintf(payload, sizeof(payload), "%s", POLL_CMD);
    // The sequence number pairs the DONE with this POLL's T1. Only a peer that has answered with a
    // stamped DONE gets one, since a legacy peer only recognizes the bare POLL.
    const PeerClock *clock = this->time_sync_enabled ? this->find_peer_clock(this->polled_peer_id) : nullptr;
    this->poll_seq = 0;
    if (clock && clock->sync_capable) {
        this->poll_seq = this->last_poll_seq = std::max<uint32_t>(1, this->last_poll_seq + 1);
        length += std::snprintf(payload + length, sizeof(payload) - length, "%lu", (unsigned long)this->poll_seq);
    }
    this->poll_sent_us = esp_timer_get_time(); // T1, stamped right before the frame enters the TX buffer
    this->poll_frame_len = this->send_message(this->polled_peer_id, payload, length);
    this->poll_start_millis = millis();
    this->is_polling = true;
}

void SerialBus::handle_done(const uint8_t sender, const int64_t *stamp, const int64_t t4, const size_t done_frame_len) {
    const bool stamped = stamp[0] >= 0;
    const int64_t seq = stamped ? stamp[2] : 0; // compared unconverted, so an oversized number never matches
    // a DONE for another POLL, e.g. a late one after a timeout, must neither end the current poll nor yield a sample
    if (!this->is_polling || sender != this->polled_peer_id || seq != this->poll_seq) {
        return;
    }
    this->is_polling = false;
    PeerClock *clock = this->time_sync_enabled ? this->find_peer_clock(sender) : nullptr;
    if (!clock) {
        return;
    }
    clock->sync_capable = stamped;
    if (seq != 0) { // the stamped answer to a bare POLL (seq 0) only proves the peer capable
        this->handle_sync_sample(*clock, stamp[0], stamp[1], t4, done_frame_len);
    }
}

int64_t SerialBus::airtime_us(const size_t frame_len) const {
    return static_cast<int64_t>(frame_len) * 10 * 1000000LL / this->serial->baud_rate; // 8N1: 10 bits per byte
}

SerialBus::PeerClock *SerialBus::find_peer_clock(const uint8_t peer_id) {
    for (PeerClock &clock : this->peer_clocks) {
        if (clock.peer_id == peer_id) {
            return &clock;
        }
    }
    return nullptr;
}

void SerialBus::handle_sync_sample(PeerClock &clock, const int64_t t3, const int64_t processing_us, const int64_t t4,
                                   const size_t done_frame_len) {
    const int64_t t1 = this->poll_sent_us;
    const int64_t t2 = t3 - processing_us;
    const int64_t transport_us = (t4 - t1) - processing_us;
    if (transport_us < 0) {
        return; // the peer's processing time exceeds our round trip: a forged stamp
    }
    // The unknown delays (TX queueing, RX-to-read on either side) add up to the transport time
    // beyond the known airtime of the two frames; they bound the error of the offset estimate.
    // The known airtime asymmetry (short POLL vs. long DONE) is corrected for, so the bound only
    // covers the unknowns.
    const int64_t poll_airtime = this->airtime_us(this->poll_frame_len);
    const int64_t done_airtime = this->airtime_us(done_frame_len);
    const int64_t residual_us = std::max<int64_t>(0, transport_us - poll_airtime - done_airtime);
    const int64_t offset_us = ((t2 - t1) + (t3 - t4)) / 2 - (poll_airtime - done_airtime) / 2;
    const int64_t accuracy_us = residual_us / 2;

    // an unlocked estimate takes the first sample, whatever its bound: the bound is published with it
    if (!clock.locked || accuracy_us <= SYNC_ACCEPT_ACCURACY_US) {
        this->accept_sync_sample(clock, offset_us, accuracy_us);
        return;
    }
    // the window starts with its first sample, so a sample after a polling pause opens a new window
    // instead of being accepted unseen
    if (!clock.window_has_sample) {
        clock.window_has_sample = true;
        clock.window_start_ms = millis();
        clock.window_offset_us = offset_us;
        clock.window_accuracy_us = accuracy_us;
    } else if (accuracy_us < clock.window_accuracy_us) {
        clock.window_offset_us = offset_us;
        clock.window_accuracy_us = accuracy_us;
    }
    if (millis_since(clock.window_start_ms) >= SYNC_WINDOW_MS) {
        this->accept_sync_sample(clock, clock.window_offset_us, clock.window_accuracy_us);
    }
}

void SerialBus::accept_sync_sample(PeerClock &clock, const int64_t offset_us, const int64_t accuracy_us) {
    clock.offset_us = offset_us;
    clock.accuracy_us = accuracy_us;
    clock.locked = true;
    clock.window_has_sample = false;
    this->publish_peer_clock(clock);
}

void SerialBus::reset_peer_clock(const uint8_t peer_id) {
    // A poll timeout invalidates the estimate (the property turns NaN); the next sample relocks it.
    // The peer may have been replaced by one that only understands bare POLLs, so start over with those.
    PeerClock *clock = this->find_peer_clock(peer_id);
    if (clock) {
        clock->sync_capable = false;
        clock->locked = false;
        clock->window_has_sample = false;
        this->publish_peer_clock(*clock);
    }
}

void SerialBus::publish_peer_clock(PeerClock &clock) {
    // a full snapshot per update, so a delayed update is harmless: the next one carries the same state
    const OffsetUpdate update{this->generation, clock.peer_id, clock.locked, clock.offset_us, clock.accuracy_us};
    clock.publish_pending = xQueueSend(this->offset_queue, &update, 0) != pdTRUE;
}

void SerialBus::publish_pending_peer_clocks() {
    for (PeerClock &clock : this->peer_clocks) {
        if (clock.publish_pending) {
            this->publish_peer_clock(clock);
        }
    }
}

bool SerialBus::parse_message(const char *message_line, IncomingMessage &message) const {
    // format: $$sender:receiver$$payload
    const std::string line(message_line);
    const size_t header_start = line.find("$$");
    if (header_start == std::string::npos || header_start != 0) {
        return false;
    }
    const size_t header_end = line.find("$$", 2);
    if (header_end == std::string::npos) {
        return false;
    }
    const size_t colon_pos = line.find(':', 2);
    if (colon_pos == std::string::npos || colon_pos >= header_end) {
        return false;
    }
    try {
        const int sender = std::stoi(line.substr(2, colon_pos - 2));
        const int receiver = std::stoi(line.substr(colon_pos + 1, header_end - (colon_pos + 1)));
        if (sender < 0 || sender > 255 || receiver < 0 || receiver > 255) {
            return false;
        }
        message.sender = static_cast<uint8_t>(sender);
        message.receiver = static_cast<uint8_t>(receiver);
    } catch (...) {
        return false;
    }
    const size_t payload_len = line.size() - (header_end + 2);
    if (payload_len >= PAYLOAD_CAPACITY) {
        return false;
    }
    message.length = payload_len;
    memcpy(message.payload, line.c_str() + header_end + 2, payload_len);
    message.payload[payload_len] = '\0';
    return true;
}

void SerialBus::handle_incoming_message(const IncomingMessage &message) {
    // echo messages from communication task (node_id == sender == receiver)
    if (this->node_id == message.sender && this->node_id == message.receiver) {
        echo("%s", message.payload);
        return;
    }

    // telemetry of peers is checked here and never processed as Lizard code
    if (message.payload[0] == telemetry::FRAME_PREFIX) {
        this->handle_telemetry_frame(message);
        return;
    }
    if (std::strncmp(message.payload, telemetry::LAYOUT_PREFIX, sizeof(telemetry::LAYOUT_PREFIX) - 1) == 0) {
        this->handle_telemetry_layout(message);
        return;
    }

    // Handle OTB frames (check prefix first to avoid function call overhead for regular messages)
    std::string_view payload_view(message.payload, message.length);
    constexpr size_t otb_prefix_len = sizeof(otb::OTB_MSG_PREFIX) - 1;
    if (payload_view.substr(0, otb_prefix_len) == otb::OTB_MSG_PREFIX &&
        otb::bus_handle_frame(this->otb_session, message.sender, payload_view)) {
        return;
    }

    // echo incoming messages from peers
    const size_t prefix_len = sizeof(ECHO_CMD) - 1;
    if (std::strncmp(message.payload, ECHO_CMD, prefix_len) == 0) {
        static char buffer[PAYLOAD_CAPACITY];
        const size_t copy_len = std::min(message.length - prefix_len, static_cast<size_t>(sizeof(buffer) - 1));
        memcpy(buffer, message.payload + prefix_len, copy_len);
        buffer[copy_len] = '\0';
        echo("bus[%u]: %s", message.sender, buffer);
        if (std::strcmp(buffer, "Ready.") == 0) {
            this->peer_layouts.erase(message.sender); // the peer booted and announces its layouts with the first poll
        }
        return;
    }

    // process control commands starting with "!" silently
    if (message.payload[0] == '!') {
        process_line(message.payload, message.length, false);
        return;
    }

    // process regular commands and relay any echo() output back to sender
    this->echo_target_id = message.sender;
    try {
        process_line(message.payload, message.length, false);
    } catch (const std::exception &e) {
        echo("error processing command: %s", e.what());
    }
    this->echo_target_id = 0;
}

void SerialBus::enqueue_outgoing_message(const uint8_t receiver, const char *payload, const size_t length, const TickType_t wait) {
    if (length >= PAYLOAD_CAPACITY) {
        throw std::runtime_error("serial bus: payload is too large for serial bus");
    }
    if (std::strchr(payload, '\n') != nullptr) {
        throw std::runtime_error("serial bus: payload must not contain newline characters");
    }
    OutgoingMessage message{receiver, length, {}};
    memcpy(message.payload, payload, length);
    message.payload[length] = '\0';
    if (xQueueSend(this->outbound_queue, &message, wait) != pdTRUE) {
        throw std::runtime_error("serial bus: could not enqueue outgoing message");
    }
}

bool SerialBus::send_outgoing_queue() {
    bool sent_any = false;
    OutgoingMessage message;
    while (xQueueReceive(this->outbound_queue, &message, 0) == pdTRUE) {
        this->send_message(message.receiver, message.payload, message.length);
        sent_any = true;
    }
    return sent_any;
}

size_t SerialBus::send_message(const uint8_t receiver, const char *payload, const size_t length) const {
    static char buffer[FRAME_BUFFER_SIZE];
    const int header_len = csprintf(buffer, sizeof(buffer), "$$%u:%u$$", this->node_id, receiver);
    if (header_len < 0) {
        throw std::runtime_error("serial bus: could not format bus header");
    }
    if (header_len + length >= sizeof(buffer)) {
        throw std::runtime_error("serial bus: payload is too large");
    }
    memcpy(buffer + header_len, payload, length);
    this->serial->write_checked_line(buffer, header_len + length);
    return header_len + length + Serial::CHECKSUM_TRAILER_LENGTH; // bytes on the wire
}

void SerialBus::print_to_incoming_queue(const char *format, ...) {
    IncomingMessage message{this->node_id, this->node_id, 0, {}};
    va_list args;
    va_start(args, format);
    message.length = std::vsnprintf(message.payload, PAYLOAD_CAPACITY, format, args);
    va_end(args);
    this->push_incoming(message);
}

void SerialBus::handle_echo(const char *line) {
    if (!this->echo_target_id) {
        return;
    }
    char payload[PAYLOAD_CAPACITY];
    const int len = std::snprintf(payload, sizeof(payload), "%s%s", ECHO_CMD, line);
    if (len < 0 || len >= sizeof(payload)) {
        echo("warning: serial bus %s failed to relay output", this->name.c_str());
        return;
    }
    try {
        this->enqueue_outgoing_message(this->echo_target_id, payload, len);
    } catch (const std::runtime_error &e) {
        // echo() calls back into handle_echo(); stop relaying so the warning is not relayed (and fails) recursively
        this->echo_target_id = 0;
        echo("warning: serial bus %s failed to relay output: %s", this->name.c_str(), e.what());
    }
}

bool SerialBus::send_telemetry_layout(const char *line, const size_t length) {
    try {
        this->enqueue_outgoing_message(this->coordinator_id, line, length, 0);
        return true;
    } catch (const std::runtime_error &) {
        return false;
    }
}

bool SerialBus::telemetry_frame_pending(const uint8_t frame_id) {
    FrameSlot *const slots = this->frame_slots;
    for (size_t i = 0; slots && i < FRAME_SLOTS; ++i) {
        if (slots[i].frame_id == frame_id) {
            taskENTER_CRITICAL(&this->frame_slot_lock);
            const bool pending = slots[i].pending;
            taskEXIT_CRITICAL(&this->frame_slot_lock);
            return pending;
        }
    }
    return false;
}

bool SerialBus::store_telemetry_frame(const uint8_t frame_id, const char *line, const size_t length, const bool replace) {
    FrameSlot *slots = this->frame_slots;
    if (!slots) {
        slots = new FrameSlot[FRAME_SLOTS];
        this->frame_slots = slots;
    }
    FrameSlot *slot = nullptr;
    for (size_t i = 0; i < FRAME_SLOTS && !slot; ++i) {
        slot = slots[i].frame_id == frame_id ? &slots[i] : nullptr;
    }
    for (size_t i = 0; i < FRAME_SLOTS && !slot; ++i) {
        slot = slots[i].frame_id == 0 ? &slots[i] : nullptr;
    }
    if (!slot) {
        if (millis_since(this->last_slot_warning_millis) > SLOT_WARNING_INTERVAL_MS) {
            this->last_slot_warning_millis = millis();
            char payload[PAYLOAD_CAPACITY];
            const int payload_length = std::snprintf(payload, sizeof(payload),
                                                     "%swarning: serial bus %s has no slot for telemetry frame %u (at most %u frames)",
                                                     ECHO_CMD, this->name.c_str(), frame_id, static_cast<unsigned>(FRAME_SLOTS));
            if (payload_length > 0 && payload_length < static_cast<int>(sizeof(payload))) {
                echo("%s", payload + sizeof(ECHO_CMD) - 1);
                try {
                    this->enqueue_outgoing_message(this->coordinator_id, payload, payload_length, 0); // printed as bus[N]: there
                } catch (const std::runtime_error &) {
                }
            }
        }
        return false;
    }
    slot->frame_id = frame_id;
    taskENTER_CRITICAL(&this->frame_slot_lock);
    const bool store = !replace || slot->pending; // a frame that left meanwhile is followed by the next sequence number
    if (store) {
        slot->pending = true;
        slot->length = length;
        memcpy(slot->line, line, length);
    }
    taskEXIT_CRITICAL(&this->frame_slot_lock);
    return store;
}

void SerialBus::clear_telemetry_slots() {
    FrameSlot *const slots = this->frame_slots;
    for (size_t i = 0; slots && i < FRAME_SLOTS; ++i) {
        taskENTER_CRITICAL(&this->frame_slot_lock);
        slots[i].pending = false;
        taskEXIT_CRITICAL(&this->frame_slot_lock);
        slots[i].frame_id = 0;
    }
}

void SerialBus::send_frame_slots(const uint8_t receiver) {
    FrameSlot *const slots = this->frame_slots;
    static char line[PAYLOAD_CAPACITY];
    for (size_t i = 0; slots && i < FRAME_SLOTS; ++i) {
        size_t length = 0;
        taskENTER_CRITICAL(&this->frame_slot_lock);
        if (slots[i].pending) {
            length = slots[i].length;
            memcpy(line, slots[i].line, length);
            slots[i].pending = false;
        }
        taskEXIT_CRITICAL(&this->frame_slot_lock);
        if (length > 0) {
            this->send_message(receiver, line, length);
        }
    }
}

bool SerialBus::passes_telemetry(const uint8_t peer_id) const {
    return this->pass_all_telemetry ||
           std::find(this->passed_peers.begin(), this->passed_peers.end(), peer_id) != this->passed_peers.end();
}

void SerialBus::handle_telemetry_frame(const IncomingMessage &message) {
    if (!this->passes_telemetry(message.sender)) {
        return;
    }
    uint8_t body[telemetry::MAX_BODY];
    const size_t length = telemetry::decode_frame(message.payload, message.length, body);
    if (length == 0) {
        this->telemetry_errors->set_integer_value(this->telemetry_errors->integer_value() + 1);
        return;
    }
    echo("bus[%u]: %s", message.sender, message.payload);

    PeerLayout &layout = this->peer_layouts[message.sender];
    const auto frame = layout.frames.find(body[0]);
    const bool known = frame != layout.frames.end() &&
                       std::find(frame->second.begin(), frame->second.end(), 0) == frame->second.end() &&
                       telemetry::payload_size(frame->second) == length - telemetry::HEADER_SIZE - telemetry::CRC_SIZE;
    if (!known && !layout.other_version) {
        // e.g. because this node booted after the peer, a layout line got lost or the peer redefined the frame
        this->request_layout(message.sender, layout);
    }
}

void SerialBus::handle_telemetry_layout(const IncomingMessage &message) {
    if (!this->passes_telemetry(message.sender)) {
        return;
    }
    telemetry::LayoutLine line;
    if (!telemetry::parse_layout(message.payload, line)) {
        this->telemetry_errors->set_integer_value(this->telemetry_errors->integer_value() + 1);
        return;
    }
    echo("bus[%u]: %s", message.sender, message.payload);

    PeerLayout &layout = this->peer_layouts[message.sender];
    if (line.version != telemetry::LAYOUT_VERSION) {
        layout.other_version = true; // only the host may read it; the peer may be updated, so until its Ready.
        return;
    }
    std::vector<char> &types = layout.frames[line.frame_id];
    if (types.size() != line.count) {
        types.assign(line.count, 0);
    }
    types[line.index] = line.type;
}

void SerialBus::request_layout(const uint8_t peer_id, PeerLayout &layout) {
    if (layout.requested && millis_since(layout.request_millis) <= LAYOUT_REQUEST_INTERVAL_MS) {
        return;
    }
    try {
        this->enqueue_outgoing_message(peer_id, LAYOUT_REQUEST, sizeof(LAYOUT_REQUEST) - 1, 0);
        layout.requested = true;
        layout.request_millis = millis();
    } catch (const std::runtime_error &) {
        // the queue is full; the next frame asks again
    }
}

void SerialBus::request_passed_layouts() {
    for (size_t i = 0; i < this->config.peer_count; ++i) {
        const uint8_t peer_id = this->config.peer_ids[i];
        if (this->passes_telemetry(peer_id)) {
            PeerLayout &layout = this->peer_layouts[peer_id];
            layout.requested = false; // asked for on purpose, however recently a frame asked
            this->request_layout(peer_id, layout);
        }
    }
}
