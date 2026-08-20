#pragma once

#include "../utils/otb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "module.h"
#include "serial.h"
#include <climits>
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

    std::vector<uint8_t> peer_ids; // guarded by sync_mux

    // --- time sync (see enable_time_sync) ---------------------------------
    // The peer stamps its esp_timer time into each DONE frame; the coordinator
    // estimates the per-peer clock offset with a windowed maximum (the least
    // delayed samples carry the least queueing latency) and publishes it as a
    // module property "offset_<id>" in milliseconds. The property is NaN until
    // the first window locks and whenever the estimate turns invalid (peer
    // dropped by make_coordinator or its poll timed out).
    struct PeerClock {
        uint8_t peer_id = 0;
        bool locked = false;
        int64_t window_max_us = INT64_MIN;
        size_t window_count = 0;
        int64_t offset_us = 0;
    };
    static constexpr size_t SYNC_WINDOW = 128;
    bool time_sync_enabled = false;
    std::vector<PeerClock> peer_clocks;
    // Guards peer_ids, peer_clocks and the polling state against the communication task: the
    // main task rebuilds them outside the lock and swaps them in, so that task never holds a
    // reference or index into storage that can reallocate under it.
    mutable portMUX_TYPE sync_mux = portMUX_INITIALIZER_UNLOCKED;
    void rebuild_peer_clocks();
    void update_peer_offset(const uint8_t sender, const int64_t raw_offset_us);
    void reset_peer_clock(const uint8_t peer_id);

    QueueHandle_t outbound_queue = nullptr;
    QueueHandle_t inbound_queue = nullptr;
    TaskHandle_t communication_task = nullptr;
    // guarded by sync_mux
    bool is_polling = false;
    unsigned long poll_start_millis = 0;
    size_t poll_index = 0;
    uint8_t requesting_node = 0;
    bool ready_pending = true;
    uint8_t echo_target_id = 0; // node ID that should receive relayed echo output (0 = no relay)
    otb::BusOtbSession otb_session;

    [[noreturn]] static void communication_loop(void *param);
    void process_uart();
    bool parse_message(const char *message_line, IncomingMessage &message) const;
    void handle_incoming_message(const IncomingMessage &message);
    void enqueue_outgoing_message(const uint8_t receiver, const char *payload, const size_t length);
    bool send_outgoing_queue();
    void send_message(const uint8_t receiver, const char *payload, const size_t length) const;

    void print_to_incoming_queue(const char *format, ...) const;
    void handle_echo(const char *line);
};
