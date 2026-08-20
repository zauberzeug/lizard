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

    // The main task configures the bus by sending a Config through config_queue; the
    // communication task adopts it at the top of its loop and exclusively owns the
    // peer list, the polling state and the clock estimates from then on, so no locking
    // is needed. Estimates travel back to the main task as OffsetUpdate snapshots via
    // offset_queue; only the main task touches the properties.
    struct Config {
        uint32_t generation; // stamps OffsetUpdates so the main task can drop ones from a superseded config
        bool time_sync_enabled;
        uint8_t peer_count;
        uint8_t peer_ids[254];
    };
    struct OffsetUpdate {
        uint32_t generation;
        uint8_t peer_id;
        bool valid;
        int64_t offset_us;
        int64_t accuracy_us;
        bool oneway_valid; // A/B prototype: remove before merge
        int64_t oneway_offset_us;
    };

    // --- time sync (see enable_time_sync) ---------------------------------
    // Four timestamps around every POLL/DONE round trip, NTP style:
    //   T1 coordinator sends POLL, T2 peer receives it, T3 peer sends DONE, T4 coordinator receives it.
    // The peer reports T3 and T3-T2 in the DONE frame; the coordinator computes
    //   offset   = ((T2-T1) + (T3-T4)) / 2    (peer clock minus coordinator clock)
    //   accuracy = (transport - known airtime) / 2, transport = (T4-T1) - (T3-T2)
    // and publishes them as "offset_<id>" and "offset_<id>_accuracy" in milliseconds.
    // The peer's processing time cancels, so a single sample locks the estimate.
    struct PeerClock {
        uint8_t peer_id = 0;
        bool locked = false;
        int64_t offset_us = 0;
        int64_t accuracy_us = 0;
        // samples above the accept threshold compete for the best of one window
        unsigned long window_start_ms = 0;
        bool window_has_sample = false;
        int64_t window_offset_us = 0;
        int64_t window_accuracy_us = 0;
        // A/B prototype (one-way estimator from PR #252): remove before merge
        bool oneway_locked = false;
        int64_t oneway_offset_us = 0;
        int64_t oneway_window_max_us = INT64_MIN;
        size_t oneway_window_count = 0;
    };

    QueueHandle_t config_queue = nullptr;
    QueueHandle_t offset_queue = nullptr;
    QueueHandle_t outbound_queue = nullptr;
    QueueHandle_t inbound_queue = nullptr;
    TaskHandle_t communication_task = nullptr;

    // --- owned by the main task --------------------------------------------
    Config config{};
    void send_config();
    void rebuild_offset_properties();
    void apply_offset_update(const OffsetUpdate &update);

    // --- owned by the communication task -----------------------------------
    std::vector<uint8_t> peer_ids;
    bool time_sync_enabled = false;
    uint32_t generation = 0;
    std::vector<PeerClock> peer_clocks;
    bool is_polling = false;
    unsigned long poll_start_millis = 0;
    int64_t poll_sent_us = 0;     // T1
    size_t poll_frame_len = 0;    // bytes of the POLL frame on the wire
    int64_t poll_received_us = 0; // T2 (peer side)
    size_t poll_index = 0;
    uint8_t requesting_node = 0;
    bool ready_pending = true;
    uint8_t echo_target_id = 0; // node ID that should receive relayed echo output (0 = no relay)
    otb::BusOtbSession otb_session;

    [[noreturn]] static void communication_loop(void *param);
    void adopt_config(const Config &config);
    void process_uart();
    bool parse_message(const char *message_line, IncomingMessage &message) const;
    void handle_incoming_message(const IncomingMessage &message);
    void enqueue_outgoing_message(const uint8_t receiver, const char *payload, const size_t length);
    bool send_outgoing_queue();
    size_t send_message(const uint8_t receiver, const char *payload, const size_t length) const;

    int64_t airtime_us(const size_t frame_len) const;
    void handle_sync_sample(const uint8_t sender, const int64_t t3, const int64_t processing_us, const int64_t t4, const size_t done_frame_len);
    void reset_peer_clock(const uint8_t peer_id);
    void publish_peer_clock(const PeerClock &clock) const;

    void print_to_incoming_queue(const char *format, ...) const;
    void handle_echo(const char *line);
    bool is_coordinator() const { return !this->peer_ids.empty(); }
};
