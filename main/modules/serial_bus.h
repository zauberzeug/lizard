#pragma once

#include "module.h"
#include "serial.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <cstdint>
#include <vector>

class SerialBus;
using SerialBus_ptr = std::shared_ptr<SerialBus>;

class SerialBus : public Module {
public:
    static constexpr size_t PAYLOAD_CAPACITY = 256;

    SerialBus(const std::string &name,
              const ConstSerial_ptr serial,
              const uint8_t node_id,
              const std::vector<uint8_t> peer_ids,
              MessageHandler message_handler);

    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

private:
    struct BusFrame {
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
    const std::vector<uint8_t> peer_ids;
    MessageHandler message_handler;

    QueueHandle_t outbound_queue = nullptr;
    QueueHandle_t inbound_queue = nullptr;
    TaskHandle_t communicator_task = nullptr;
    bool waiting_for_done = false;
    uint8_t current_poll_target = 0;
    unsigned long poll_start_millis = 0;
    size_t poll_index = 0;
    bool transmit_window_open = false;
    uint8_t window_requester = 0;
    unsigned long last_message_millis = 0;

    void start_communicator();
    static void communicator_task_trampoline(void *param);
    [[noreturn]] void communicator_loop();
    void communicator_process_uart();
    bool flush_outgoing_queue();
    void push_incoming_frame(const BusFrame &frame);
    void drain_inbox();
    void enqueue_message(uint8_t receiver, const char *payload, size_t length);
    void send_frame(uint8_t receiver, const char *payload, size_t length) const;
    void send_done(uint8_t receiver) const;
    bool parse_frame(const char *line, BusFrame &frame) const;
    void handle_frame(const BusFrame &frame);
    void handle_poll_request(uint8_t sender);
    void handle_done(uint8_t sender);
    void coordinator_poll_step();
    void check_poll_timeout();
    bool is_coordinator() const;
};
