#include "serial_bus.h"

#include "../global.h"
#include "../utils/string_utils.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <stdexcept>

extern void process_line(const char *line, const int len);

static constexpr size_t FRAME_BUFFER_SIZE = 512;
static constexpr unsigned long POLL_TIMEOUT_MS = 250;
static constexpr size_t OUTGOING_QUEUE_LENGTH = 32;
static constexpr size_t INCOMING_QUEUE_LENGTH = 32;
static constexpr const char ECHO_CMD[] = "__ECHO__";
static constexpr const char POLL_CMD[] = "__POLL__";
static constexpr const char DONE_CMD[] = "__DONE__";
static constexpr const char SUBSCRIBE_CMD[] = "__SUBSCRIBE__:";
static constexpr const char UPDATE_CMD[] = "__UPDATE__:";

REGISTER_MODULE_DEFAULTS(SerialBus)

const std::map<std::string, Variable_ptr> SerialBus::get_defaults() {
    return {};
}

SerialBus::SerialBus(const std::string &name, const ConstSerial_ptr serial, const uint8_t node_id)
    : Module(serial_bus, name), serial(serial), node_id(node_id) {
    this->properties = SerialBus::get_defaults();
    this->serial->enable_line_detection();

    if (!(this->outbound_queue = xQueueCreate(OUTGOING_QUEUE_LENGTH, sizeof(OutgoingMessage)))) {
        throw std::runtime_error("failed to create serial bus outbound queue");
    }
    if (!(this->inbound_queue = xQueueCreate(INCOMING_QUEUE_LENGTH, sizeof(IncomingMessage)))) {
        vQueueDelete(this->outbound_queue);
        throw std::runtime_error("failed to create serial bus inbound queue");
    }

    if (xTaskCreatePinnedToCore(
            SerialBus::communication_loop, "serial_bus_comm", 4096, this, 5, &this->communication_task, 1) != pdPASS) {
        vQueueDelete(this->outbound_queue);
        vQueueDelete(this->inbound_queue);
        throw std::runtime_error("failed to create serial bus communication task");
    }

    register_echo_callback([this](const char *line) { this->handle_echo(line); });
}

void SerialBus::step() {
    IncomingMessage message;
    while (xQueueReceive(this->inbound_queue, &message, 0) == pdTRUE) {
        this->handle_incoming_message(message);
    }
    this->send_subscription_updates();
    Module::step();
}

void SerialBus::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "send") {
        Module::expect(arguments, 2, integer, string);
        const int receiver = arguments[0]->evaluate_integer();
        if (receiver <= 0 || receiver >= 255) {
            throw std::runtime_error("receiver ID must be between 0 and 255");
        }
        const std::string payload = arguments[1]->evaluate_string();
        this->enqueue_outgoing_message(static_cast<uint8_t>(receiver), payload.c_str(), payload.size());
    } else if (method_name == "subscribe") {
        if (arguments.size() < 2 || arguments.size() > 3) {
            throw std::runtime_error("subscribe expects 2 or 3 arguments: node_id, \"module.property\"[, \"local_name\"]");
        }
        Module::expect(arguments, -1, integer, string, string);
        const int node_id = arguments[0]->evaluate_integer();
        if (node_id <= 0 || node_id >= 255) {
            throw std::runtime_error("node id must be between 1 and 254");
        }
        const std::string remote_path = arguments[1]->evaluate_string();
        if (remote_path.find('.') == std::string::npos) {
            throw std::runtime_error("property path must be \"module.property\"");
        }
        std::string local_name;
        if (arguments.size() == 3) {
            local_name = arguments[2]->evaluate_string();
        } else {
            local_name = remote_path;
            std::replace(local_name.begin(), local_name.end(), '.', '_');
        }
        if (!this->properties.count(local_name)) {
            this->properties[local_name] = std::make_shared<NumberVariable>();
        }
        this->subscribe(static_cast<uint8_t>(node_id), remote_path, this->name + "." + local_name);
    } else if (method_name == "make_coordinator") {
        if (arguments.empty()) {
            throw std::runtime_error("make_coordinator expects at least one peer ID");
        }
        std::vector<uint8_t> peers;
        peers.reserve(arguments.size());
        for (const auto &argument : arguments) {
            if ((argument->type & integer) == 0) {
                throw std::runtime_error("peer IDs must be integers");
            }
            const long peer_value = argument->evaluate_integer();
            if (peer_value <= 0 || peer_value >= 255) {
                throw std::runtime_error("peer IDs must be between 0 and 255");
            }
            peers.push_back(static_cast<uint8_t>(peer_value));
        }
        this->peer_ids = peers;
    } else {
        Module::call(method_name, arguments);
    }
}

[[noreturn]] void SerialBus::communication_loop(void *param) {
    SerialBus *bus = static_cast<SerialBus *>(param);
    while (true) {
        bus->process_uart();
        if (bus->is_coordinator()) {
            // poll next peer
            if (!bus->is_polling && !bus->send_outgoing_queue()) {
                bus->poll_index = (bus->poll_index + 1) % bus->peer_ids.size();
                bus->send_message(bus->peer_ids[bus->poll_index], POLL_CMD, sizeof(POLL_CMD) - 1);
                bus->poll_start_millis = millis();
                bus->is_polling = true;
            }
            // handle poll timeout
            if (bus->is_polling && millis_since(bus->poll_start_millis) > POLL_TIMEOUT_MS) {
                bus->print_to_incoming_queue("warning: serial bus %s poll to %u timed out", bus->name.c_str(), bus->peer_ids[bus->poll_index]);
                bus->is_polling = false;
            }
        } else {
            // respond to poll
            if (bus->requesting_node) {
                try {
                    bus->send_outgoing_queue();
                    bus->send_message(bus->requesting_node, DONE_CMD, sizeof(DONE_CMD) - 1);
                } catch (const std::exception &e) {
                    bus->print_to_incoming_queue("warning: serial bus %s error while responding to poll: %s", bus->name.c_str(), e.what());
                }
                bus->requesting_node = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void SerialBus::process_uart() {
    static char buffer[FRAME_BUFFER_SIZE];
    while (this->serial->has_buffered_lines()) {
        const int len = this->serial->read_line(buffer, sizeof(buffer));
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
            this->requesting_node = message.sender;
            continue;
        }

        // handle done command
        if (std::strcmp(message.payload, DONE_CMD) == 0) {
            if (message.sender == this->peer_ids[this->poll_index]) {
                this->is_polling = false;
            }
            continue;
        }

        // enqueue message in inbound queue
        if (xQueueSend(this->inbound_queue, &message, 0) != pdTRUE) {
            this->print_to_incoming_queue("warning: serial bus %s could not enqueue message: %s", this->name.c_str(), buffer);
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

    // handle subscription request: "__SUBSCRIBE__:module.property:local_name"
    const size_t sub_len = sizeof(SUBSCRIBE_CMD) - 1;
    if (std::strncmp(message.payload, SUBSCRIBE_CMD, sub_len) == 0) {
        const char *rest = message.payload + sub_len;
        const char *dot = std::strchr(rest, '.');
        const char *colon = std::strchr(rest, ':');
        if (dot && colon && dot < colon) {
            const std::string module_name(rest, dot);
            const std::string property_name(dot + 1, colon);
            Variable_ptr prop = Global::get_module(module_name)->get_property(property_name);
            if (!std::any_of(subscriptions.begin(), subscriptions.end(),
                             [&](const Subscription &s) { return s.subscriber == message.sender && s.property == prop; })) {
                Subscription sub{message.sender, prop, {}};
                std::strncpy(sub.local_name, colon + 1, sizeof(sub.local_name) - 1);
                sub.local_name[sizeof(sub.local_name) - 1] = '\0';
                subscriptions.push_back(sub);
            }
        }
        return;
    }

    // handle subscription update: "__UPDATE__:module.property=value"
    const size_t upd_len = sizeof(UPDATE_CMD) - 1;
    if (std::strncmp(message.payload, UPDATE_CMD, upd_len) == 0) {
        try {
            process_line(message.payload + upd_len, message.length - upd_len);
        } catch (...) {}
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

void SerialBus::enqueue_outgoing_message(uint8_t receiver, const char *payload, size_t length) {
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

void SerialBus::send_message(uint8_t receiver, const char *payload, size_t length) const {
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
}

void SerialBus::subscribe(uint8_t node, const std::string &remote_path, const std::string &local_name) {
    char payload[PAYLOAD_CAPACITY];
    const int len = csprintf(payload, sizeof(payload), "%s%s:%s", SUBSCRIBE_CMD, remote_path.c_str(), local_name.c_str());
    if (len > 0)
        this->enqueue_outgoing_message(node, payload, len);
}

void SerialBus::send_subscription_updates() {
    for (const Subscription &sub : this->subscriptions) {
        char payload[PAYLOAD_CAPACITY];
        int len = csprintf(payload, sizeof(payload), "%s%s=", UPDATE_CMD, sub.local_name);
        if (len <= 0)
            continue;
        len += sub.property->print_to_buffer(payload + len, sizeof(payload) - len);
        this->enqueue_outgoing_message(sub.subscriber, payload, len);
    }
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
