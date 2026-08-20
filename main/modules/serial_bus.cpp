#include "serial_bus.h"

#include "../utils/format.h"
#include "../utils/otb.h"
#include "../utils/string_utils.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "module_helpers.h"
#include "serial.h"
#include <esp_timer.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

extern void process_line(const char *line, const int len);

static constexpr size_t FRAME_BUFFER_SIZE = 512;
static constexpr unsigned long POLL_TIMEOUT_MS = 250;
static constexpr size_t OUTGOING_QUEUE_LENGTH = 32;
static constexpr size_t INCOMING_QUEUE_LENGTH = 32;
static constexpr size_t OFFSET_QUEUE_LENGTH = 32;
static constexpr const char ECHO_CMD[] = "__ECHO__";
static constexpr const char POLL_CMD[] = "__POLL__";
static constexpr const char DONE_CMD[] = "__DONE__";
static constexpr size_t MAX_STAMP_DIGITS = 18; // beyond this strtoll saturates and skews the offset

// Estimator tunables (to be tuned on real hardware):
// a sample whose accuracy bound is at most SYNC_ACCEPT_ACCURACY_US replaces the estimate immediately;
// otherwise the best sample within SYNC_WINDOW_MS is taken, so that the estimate keeps following
// crystal drift (~20 ppm, i.e. ~20 us per second) even when the bus is busy.
static constexpr int64_t SYNC_ACCEPT_ACCURACY_US = 500;
static constexpr unsigned long SYNC_WINDOW_MS = 2000;
static constexpr size_t ONEWAY_SYNC_WINDOW = 128; // A/B prototype: remove before merge

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

const std::map<std::string, Variable_ptr> SerialBus::get_defaults() {
    return {};
}

SerialBus::SerialBus(const std::string &name, const ConstSerial_ptr serial, const uint8_t node_id)
    : Module(name), serial(serial), node_id(node_id) {
    this->properties = SerialBus::get_defaults();
    this->serial->enable_line_detection();

    if (!(this->config_queue = xQueueCreate(1, sizeof(Config)))) {
        throw std::runtime_error("failed to create serial bus config queue");
    }
    if (!(this->offset_queue = xQueueCreate(OFFSET_QUEUE_LENGTH, sizeof(OffsetUpdate)))) {
        vQueueDelete(this->config_queue);
        throw std::runtime_error("failed to create serial bus offset queue");
    }
    if (!(this->outbound_queue = xQueueCreate(OUTGOING_QUEUE_LENGTH, sizeof(OutgoingMessage)))) {
        vQueueDelete(this->config_queue);
        vQueueDelete(this->offset_queue);
        throw std::runtime_error("failed to create serial bus outbound queue");
    }
    if (!(this->inbound_queue = xQueueCreate(INCOMING_QUEUE_LENGTH, sizeof(IncomingMessage)))) {
        vQueueDelete(this->config_queue);
        vQueueDelete(this->offset_queue);
        vQueueDelete(this->outbound_queue);
        throw std::runtime_error("failed to create serial bus inbound queue");
    }

    if (xTaskCreatePinnedToCore(
            SerialBus::communication_loop, "serial_bus_comm", 4096, this, 5, &this->communication_task, 1) != pdPASS) {
        vQueueDelete(this->config_queue);
        vQueueDelete(this->offset_queue);
        vQueueDelete(this->outbound_queue);
        vQueueDelete(this->inbound_queue);
        throw std::runtime_error("failed to create serial bus communication task");
    }

    register_echo_callback([this](const char *line) { this->handle_echo(line); });

    this->otb_session.bus_name = this->name.c_str();
    this->otb_session.send_fn = [this](uint8_t receiver, const char *data, size_t len) {
        this->enqueue_outgoing_message(receiver, data, len);
    };
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
            if ((argument->type & integer) == 0) {
                throw std::runtime_error("peer IDs must be integers");
            }
            const long peer_value = argument->evaluate_integer();
            if (peer_value <= 0 || peer_value >= 255) {
                throw std::runtime_error("peer IDs must be between 0 and 255");
            }
            peer_ids[peer_count++] = static_cast<uint8_t>(peer_value);
        }
        std::memcpy(this->config.peer_ids, peer_ids, peer_count);
        this->config.peer_count = peer_count;
        this->send_config();
    } else if (method_name == "enable_time_sync") {
        Module::expect(arguments, 0);
        this->config.time_sync_enabled = true;
        this->send_config();
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
        if (name.rfind("offset_", 0) == 0) {
            property->number_value = std::numeric_limits<double>::quiet_NaN();
        }
    }
    for (size_t i = 0; i < this->config.peer_count; ++i) {
        const std::string prefix = "offset_" + std::to_string(this->config.peer_ids[i]);
        for (const std::string &name : {prefix, prefix + "_accuracy", prefix + "_oneway"}) {
            if (this->properties.find(name) == this->properties.end()) {
                this->properties[name] = std::make_shared<NumberVariable>(std::numeric_limits<double>::quiet_NaN());
            }
        }
    }
}

void SerialBus::apply_offset_update(const OffsetUpdate &update) {
    if (update.generation != this->config.generation) {
        return; // estimate from a superseded config; its properties were already reset
    }
    const std::string prefix = "offset_" + std::to_string(update.peer_id);
    const auto offset = this->properties.find(prefix);
    const auto accuracy = this->properties.find(prefix + "_accuracy");
    const auto oneway = this->properties.find(prefix + "_oneway");
    if (offset == this->properties.end() || accuracy == this->properties.end() || oneway == this->properties.end()) {
        return;
    }
    offset->second->number_value = update.valid ? update.offset_us / 1000.0 : std::numeric_limits<double>::quiet_NaN();
    accuracy->second->number_value = update.valid ? update.accuracy_us / 1000.0 : std::numeric_limits<double>::quiet_NaN();
    oneway->second->number_value = update.oneway_valid ? update.oneway_offset_us / 1000.0 : std::numeric_limits<double>::quiet_NaN();
}

[[noreturn]] void SerialBus::communication_loop(void *param) {
    SerialBus *bus = static_cast<SerialBus *>(param);
    Config config;
    while (true) {
        if (xQueueReceive(bus->config_queue, &config, 0) == pdTRUE) {
            bus->adopt_config(config);
        }
        bus->process_uart();
        if (bus->is_coordinator()) {
            // poll next peer
            if (!bus->is_polling && !bus->send_outgoing_queue()) {
                bus->poll_index = (bus->poll_index + 1) % bus->peer_ids.size();
                bus->poll_sent_us = esp_timer_get_time(); // T1, stamped right before the frame enters the TX buffer
                bus->poll_frame_len = bus->send_message(bus->peer_ids[bus->poll_index], POLL_CMD, sizeof(POLL_CMD) - 1);
                bus->poll_start_millis = millis();
                bus->is_polling = true;
            }
            // handle poll timeout
            if (bus->is_polling && millis_since(bus->poll_start_millis) > POLL_TIMEOUT_MS) {
                const uint8_t timed_out = bus->peer_ids[bus->poll_index];
                bus->print_to_incoming_queue("warning: serial bus %s poll to %u timed out", bus->name.c_str(), timed_out);
                bus->is_polling = false;
                if (bus->time_sync_enabled) {
                    bus->reset_peer_clock(timed_out);
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
                    if (bus->time_sync_enabled) {
                        // T3 is stamped as late as possible; the elapsed time since T2 lets the
                        // coordinator cancel everything that happened on this side in between
                        const int64_t t3 = esp_timer_get_time();
                        char done_payload[64];
                        const int done_len = std::snprintf(done_payload, sizeof(done_payload), "%s%lld,%lld",
                                                           DONE_CMD, (long long)t3, (long long)(t3 - bus->poll_received_us));
                        bus->send_message(bus->requesting_node, done_payload, done_len);
                    } else {
                        bus->send_message(bus->requesting_node, DONE_CMD, sizeof(DONE_CMD) - 1);
                    }
                } catch (const std::exception &e) {
                    bus->print_to_incoming_queue("warning: serial bus %s error while responding to poll: %s", bus->name.c_str(), e.what());
                }
                bus->requesting_node = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
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
        clock.window_start_ms = millis();
        this->peer_clocks.push_back(clock);
    }
    // restart polling from a known-good index
    this->poll_index = 0;
    this->is_polling = false;
}

// Parses "<digits>[,<digits>]" with at most MAX_STAMP_DIGITS per field; returns false for anything
// else, which then counts as a user payload that merely shares the DONE prefix.
static bool parse_done_stamp(const char *stamp, int64_t &t3, int64_t &processing_us) {
    t3 = -1;
    processing_us = -1;
    if (*stamp == '\0') {
        return true;
    }
    int64_t *fields[] = {&t3, &processing_us};
    const char *c = stamp;
    for (size_t field = 0; field < 2; ++field) {
        const char *start = c;
        while (std::isdigit(static_cast<unsigned char>(*c))) {
            ++c;
        }
        const size_t digits = c - start;
        if (digits == 0 || digits > MAX_STAMP_DIGITS) {
            return false;
        }
        *fields[field] = std::strtoll(start, nullptr, 10);
        if (*c == '\0') {
            return true;
        }
        if (*c != ',' || field == 1) {
            return false;
        }
        ++c;
    }
    return false;
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

        // handle poll command
        if (std::strcmp(message.payload, POLL_CMD) == 0) {
            this->poll_received_us = esp_timer_get_time(); // T2
            this->requesting_node = message.sender;
            continue;
        }

        // handle done command, optionally carrying the peer's T3 stamp and its T3-T2 processing time in us
        if (std::strncmp(message.payload, DONE_CMD, sizeof(DONE_CMD) - 1) == 0) {
            int64_t t3, processing_us;
            if (parse_done_stamp(message.payload + sizeof(DONE_CMD) - 1, t3, processing_us)) {
                const int64_t t4 = esp_timer_get_time();
                if (this->is_coordinator() && message.sender == this->peer_ids[this->poll_index]) {
                    this->is_polling = false;
                    if (this->time_sync_enabled && t3 >= 0) {
                        // len is the raw frame length incl. newline, i.e. the bytes on the wire
                        this->handle_sync_sample(message.sender, t3, processing_us, t4, len);
                    }
                }
                continue;
            }
        }

        // enqueue message in inbound queue
        if (xQueueSend(this->inbound_queue, &message, 0) != pdTRUE) {
            this->print_to_incoming_queue("warning: serial bus %s could not enqueue message: %s", this->name.c_str(), buffer);
        }
    }
}

int64_t SerialBus::airtime_us(const size_t frame_len) const {
    return static_cast<int64_t>(frame_len) * 10 * 1000000LL / this->serial->baud_rate; // 8N1: 10 bits per byte
}

void SerialBus::handle_sync_sample(const uint8_t sender, const int64_t t3, const int64_t processing_us, const int64_t t4, const size_t done_frame_len) {
    const auto clock = std::find_if(this->peer_clocks.begin(), this->peer_clocks.end(),
                                    [sender](const PeerClock &candidate) { return candidate.peer_id == sender; });
    if (clock == this->peer_clocks.end()) {
        return;
    }
    const int64_t poll_airtime = this->airtime_us(this->poll_frame_len);
    const int64_t done_airtime = this->airtime_us(done_frame_len);

    // A/B prototype (one-way estimator from PR #252): remove before merge
    clock->oneway_window_max_us = std::max(clock->oneway_window_max_us, t3 - (t4 - done_airtime));
    if (++clock->oneway_window_count >= ONEWAY_SYNC_WINDOW) {
        if (!clock->oneway_locked) {
            clock->oneway_offset_us = clock->oneway_window_max_us;
            clock->oneway_locked = true;
        } else {
            clock->oneway_offset_us += (clock->oneway_window_max_us - clock->oneway_offset_us) / 2;
        }
        clock->oneway_window_max_us = INT64_MIN;
        clock->oneway_window_count = 0;
    }

    if (processing_us >= 0) {
        const int64_t t1 = this->poll_sent_us;
        const int64_t t2 = t3 - processing_us;
        const int64_t transport_us = (t4 - t1) - processing_us;
        if (transport_us < 0) {
            return; // the peer's processing time exceeds our round trip: a stale or forged stamp
        }
        // The unknown delays (TX queueing, RX-to-read on either side) add up to the transport time
        // beyond the known airtime of the two frames; they bound the error of the offset estimate.
        // The known airtime asymmetry (short POLL vs. long DONE) is corrected for, so the bound only
        // covers the unknowns.
        const int64_t residual_us = std::max<int64_t>(0, transport_us - poll_airtime - done_airtime);
        const int64_t offset_us = ((t2 - t1) + (t3 - t4)) / 2 - (poll_airtime - done_airtime) / 2;
        const int64_t accuracy_us = residual_us / 2;

        bool accept = accuracy_us <= SYNC_ACCEPT_ACCURACY_US;
        if (!accept) {
            if (!clock->window_has_sample || accuracy_us < clock->window_accuracy_us) {
                clock->window_has_sample = true;
                clock->window_offset_us = offset_us;
                clock->window_accuracy_us = accuracy_us;
            }
            if (millis_since(clock->window_start_ms) >= SYNC_WINDOW_MS) {
                accept = true;
            }
        }
        if (accept) {
            const bool direct = accuracy_us <= SYNC_ACCEPT_ACCURACY_US;
            clock->offset_us = direct ? offset_us : clock->window_offset_us;
            clock->accuracy_us = direct ? accuracy_us : clock->window_accuracy_us;
            clock->locked = true;
            clock->window_start_ms = millis();
            clock->window_has_sample = false;
        }
    }

    this->publish_peer_clock(*clock);
}

void SerialBus::reset_peer_clock(const uint8_t peer_id) {
    // A poll timeout invalidates the estimate (the property turns NaN); the next accepted sample relocks it.
    for (auto &clock : this->peer_clocks) {
        if (clock.peer_id == peer_id) {
            clock.locked = false;
            clock.window_start_ms = millis();
            clock.window_has_sample = false;
            clock.oneway_locked = false;
            clock.oneway_window_max_us = INT64_MIN;
            clock.oneway_window_count = 0;
            this->publish_peer_clock(clock);
            break;
        }
    }
}

void SerialBus::publish_peer_clock(const PeerClock &clock) const {
    // a full snapshot per update, so a dropped update is harmless: the next one carries the same state
    const OffsetUpdate update{this->generation, clock.peer_id, clock.locked, clock.offset_us, clock.accuracy_us,
                              clock.oneway_locked, clock.oneway_offset_us};
    xQueueSend(this->offset_queue, &update, 0);
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
        return;
    }

    // process control commands starting with "!" silently
    if (message.payload[0] == '!') {
        process_line(message.payload, message.length);
        return;
    }

    // process regular commands and relay any echo() output back to sender
    this->echo_target_id = message.sender;
    try {
        process_line(message.payload, message.length);
    } catch (const std::exception &e) {
        echo("error processing command: %s", e.what());
    }
    this->echo_target_id = 0;
}

void SerialBus::enqueue_outgoing_message(const uint8_t receiver, const char *payload, const size_t length) {
    if (length >= PAYLOAD_CAPACITY) {
        throw std::runtime_error("serial bus: payload is too large for serial bus");
    }
    if (std::strchr(payload, '\n') != nullptr) {
        throw std::runtime_error("serial bus: payload must not contain newline characters");
    }
    OutgoingMessage message{receiver, length, {}};
    memcpy(message.payload, payload, length);
    message.payload[length] = '\0';
    if (xQueueSend(this->outbound_queue, &message, pdMS_TO_TICKS(50)) != pdTRUE) {
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
    return header_len + length + 4; // bytes on the wire incl. the "@xx\n" checksum trailer
}

void SerialBus::print_to_incoming_queue(const char *format, ...) const {
    IncomingMessage message{this->node_id, this->node_id, 0, {}};
    va_list args;
    va_start(args, format);
    message.length = std::vsnprintf(message.payload, PAYLOAD_CAPACITY, format, args);
    va_end(args);
    xQueueSend(this->inbound_queue, &message, 0);
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
        echo("warning: serial bus %s failed to relay output: %s", this->name.c_str(), e.what());
    }
}
