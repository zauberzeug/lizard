#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "module.h"
#include "serial.h"
#include <cstdint>
#include <vector>

class SerialBus : public Module {
public:
    static constexpr size_t PAYLOAD_CAPACITY = 256;

    SerialBus(const std::string &name, const ConstSerial_ptr serial, const uint8_t node_id);

    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

private:
    struct IncomingMessage {
        uint8_t sender;
        uint8_t receiver;
        uint16_t length;
        char payload[PAYLOAD_CAPACITY];
    };
    struct OutgoingMessage {
        uint8_t receiver;
        uint16_t length;
        char payload[PAYLOAD_CAPACITY];
    };

    const ConstSerial_ptr serial;
    const uint8_t node_id;
    std::vector<uint8_t> peer_ids;

    QueueHandle_t outbound_queue = nullptr;
    QueueHandle_t inbound_queue = nullptr;
    TaskHandle_t communication_task = nullptr;
    uint8_t current_poll_target = 0xff; // 0xff = BROADCAST_ID used as sentinel for "no target"
    unsigned long poll_start_millis = 0;
    size_t poll_index = 0;
    bool transmit_window_open = false;
    uint8_t window_requester = 0;
    unsigned long last_message_millis = 0;

    // Echo callback state: tracks which target should receive relayed output
    uint8_t echo_target_id = 0xff; // 0xff = no relay, else = target ID

    [[noreturn]] static void communication_loop(void *param);
    void process_uart();
    bool send_outgoing_queue();
    void enqueue_message(uint8_t receiver, const char *payload, size_t length);
    void send_message(uint8_t receiver, const char *payload, size_t length) const;
    void handle_echo(const char *line);
    bool parse_message(const char *line, IncomingMessage &message) const;
    void handle_message(const IncomingMessage &message);
    void echo_queue(const char *format, ...) const;
    bool is_coordinator() const { return !this->peer_ids.empty(); }
};
