#pragma once

#include "../utils/otb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "module.h"
#include "serial.h"
#include <atomic>
#include <cstdint>
#include <vector>

class SerialBus;
using SerialBus_ptr = std::shared_ptr<SerialBus>;

class SerialBus : public Module {
public:
    static inline constexpr const char *TYPE = "SerialBus";

    static constexpr size_t PAYLOAD_CAPACITY = 256;

    const ConstSerial_ptr serial;
    const uint8_t node_id;

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
    struct PeerPollState {
        unsigned long backoff_start_millis = 0;
        unsigned long backoff_duration_ms = 0; // 0 == no timeout seen yet since the last successful poll
        bool unreachable = false;              // for one-shot unreachable/reachable-again reporting
    };

    std::vector<uint8_t> peer_ids;
    std::vector<PeerPollState> peer_poll_states; // indexed like peer_ids, resized together under config_mux
    // guards all reads/writes of peer_ids + peer_poll_states + poll_index + is_polling
    // across the communication_loop task boundary (main task writes via make_coordinator)
    portMUX_TYPE config_mux = portMUX_INITIALIZER_UNLOCKED;

    QueueHandle_t outbound_queue = nullptr;
    QueueHandle_t inbound_queue = nullptr;
    // written by the communication task, drained and reported by step() on the main task
    std::atomic<unsigned> dropped_inbound{0};
    unsigned long last_drop_report_millis = 0;
    TaskHandle_t communication_task = nullptr;
    // atomic so the communication task's unguarded pre-checks are race-free against
    // make_coordinator on the other core; consistent multi-field decisions still take config_mux
    std::atomic<bool> is_polling{false};
    std::atomic<unsigned long> poll_start_millis{0};
    size_t poll_index = 0;
    uint8_t requesting_node = 0;
    bool ready_pending = true;
    uint8_t echo_target_id = 0; // node ID that should receive relayed echo output (0 = no relay)
    otb::BusOtbSession otb_session;

    [[noreturn]] static void communication_loop(void *param);
    void process_uart();
    void push_incoming(const IncomingMessage &message);
    size_t next_pollable_peer() const;
    bool handle_poll_success(uint8_t &peer_id);
    bool handle_poll_timeout(uint8_t &peer_id, unsigned long &backoff_ms);
    bool parse_message(const char *message_line, IncomingMessage &message) const;
    void handle_incoming_message(const IncomingMessage &message);
    void enqueue_outgoing_message(const uint8_t receiver, const char *payload, const size_t length);
    bool send_outgoing_queue();
    void send_message(const uint8_t receiver, const char *payload, const size_t length) const;

    void print_to_incoming_queue(const char *format, ...);
    void handle_echo(const char *line);
    // atomic flag instead of peer_ids.empty(): the communication task calls this without
    // config_mux while make_coordinator swaps the vectors on the other core
    std::atomic<bool> coordinator{false};
    bool is_coordinator() const { return this->coordinator; }
};
