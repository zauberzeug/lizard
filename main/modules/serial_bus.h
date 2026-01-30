#pragma once

#include "../utils/ota.h"
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
        size_t length;
        char payload[PAYLOAD_CAPACITY];
    };
    struct OutgoingMessage {
        uint8_t receiver;
        size_t length;
        char payload[PAYLOAD_CAPACITY];
    };

    const ConstSerial_ptr serial;
    const uint8_t node_id;
    std::vector<uint8_t> peer_ids;

    QueueHandle_t outbound_queue = nullptr;
    QueueHandle_t inbound_queue = nullptr;
    TaskHandle_t communication_task = nullptr;
    bool is_polling = false;
    unsigned long poll_start_millis = 0;
    size_t poll_index = 0;
    uint8_t requesting_node = 0;
    uint8_t echo_target_id = 0; // node ID that should receive relayed echo output (0 = no relay)
    ota::BusOtaSession ota_session;

    [[noreturn]] static void communication_loop(void *param);
    void process_uart();
    bool parse_message(const char *message_line, IncomingMessage &message) const;
    void handle_incoming_message(const IncomingMessage &message);
    void enqueue_outgoing_message(uint8_t receiver, const char *payload, size_t length);
    bool send_outgoing_queue();
    void send_message(uint8_t receiver, const char *payload, size_t length) const;

    void print_to_incoming_queue(const char *format, ...) const;
    void handle_echo(const char *line);
    bool is_coordinator() const { return !this->peer_ids.empty(); }
};
