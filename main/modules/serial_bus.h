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
    };
    struct OffsetProperties {
        uint8_t peer_id;
        Variable_ptr offset;
        Variable_ptr accuracy;
    };

    // --- time sync (see enable_time_sync) ---------------------------------
    // Four timestamps around every POLL/DONE round trip, NTP style:
    //   T1 coordinator sends POLL, T2 peer receives it, T3 peer sends DONE, T4 coordinator receives it.
    // The peer reports T3 and T3-T2 in the DONE frame; the coordinator computes
    //   offset   = ((T2-T1) + (T3-T4)) / 2    (peer clock minus coordinator clock)
    //   accuracy = (transport - known airtime) / 2, transport = (T4-T1) - (T3-T2)
    // and publishes them as "offset_<id>" and "offset_<id>_accuracy" in milliseconds.
    // The peer's processing time cancels, so a single sample locks the estimate.
    // A sequence number in the POLL, echoed in the DONE, pairs each sample with its own T1.
    struct PeerClock {
        uint8_t peer_id = 0;
        bool sync_capable = false; // answered with a stamped DONE, so it understands sequenced POLLs
        bool locked = false;
        int64_t offset_us = 0;
        int64_t accuracy_us = 0;
        bool publish_pending = false; // the last snapshot did not fit into offset_queue; retry
        // samples above the accept threshold compete for the best of one window
        bool window_has_sample = false;
        unsigned long window_start_ms = 0;
        int64_t window_offset_us = 0;
        int64_t window_accuracy_us = 0;
    };

    QueueHandle_t config_queue = nullptr;
    QueueHandle_t offset_queue = nullptr;
    QueueHandle_t outbound_queue = nullptr;
    QueueHandle_t inbound_queue = nullptr;
    // written by the communication task, drained and reported by step() on the main task
    std::atomic<unsigned> dropped_inbound{0};
    unsigned long last_drop_report_millis = 0;
    TaskHandle_t communication_task = nullptr;

    // --- owned by the main task --------------------------------------------
    Config config{};
    std::vector<OffsetProperties> offset_properties;
    void send_config();
    void rebuild_offset_properties();
    void apply_offset_update(const OffsetUpdate &update);

    // --- owned by the communication task -----------------------------------
    Config received_config{}; // receive buffer for config_queue, kept off the task's stack
    std::vector<uint8_t> peer_ids;
    bool time_sync_enabled = false;
    uint32_t generation = 0;
    std::vector<PeerClock> peer_clocks;
    size_t poll_index = 0;
    bool is_polling = false;
    uint8_t polled_peer_id = 0; // peer whose DONE is outstanding; survives a reconfiguration
    unsigned long poll_start_millis = 0;
    uint32_t poll_seq = 0;      // sequence number of the outstanding POLL, 0 for a bare POLL
    uint32_t last_poll_seq = 0; // last sequence number handed out
    int64_t poll_sent_us = 0;   // T1
    size_t poll_frame_len = 0;  // bytes of the POLL frame on the wire
    uint8_t requesting_node = 0;
    int64_t poll_received_us = 0;   // T2 (peer side)
    uint32_t poll_received_seq = 0; // sequence number of the POLL being answered (peer side)
    bool ready_pending = true;
    uint8_t echo_target_id = 0; // node ID that should receive relayed echo output (0 = no relay)
    otb::BusOtbSession otb_session;

    [[noreturn]] static void communication_loop(void *param);
    void adopt_config(const Config &config);
    void process_uart();
    void push_incoming(const IncomingMessage &message);
    bool parse_message(const char *message_line, IncomingMessage &message) const;
    void handle_incoming_message(const IncomingMessage &message);
    void enqueue_outgoing_message(const uint8_t receiver, const char *payload, const size_t length);
    bool send_outgoing_queue();
    size_t send_message(const uint8_t receiver, const char *payload, const size_t length) const;

    void send_poll();
    void handle_done(const uint8_t sender, const int64_t *stamp, const int64_t t4, const size_t done_frame_len);
    int64_t airtime_us(const size_t frame_len) const;
    PeerClock *find_peer_clock(const uint8_t peer_id);
    void handle_sync_sample(PeerClock &clock, const int64_t t3, const int64_t processing_us, const int64_t t4,
                            const size_t done_frame_len);
    void accept_sync_sample(PeerClock &clock, const int64_t offset_us, const int64_t accuracy_us);
    void reset_peer_clock(const uint8_t peer_id);
    void publish_peer_clock(PeerClock &clock);
    void publish_pending_peer_clocks();

    void print_to_incoming_queue(const char *format, ...);
    void handle_echo(const char *line);
    bool is_coordinator() const { return !this->peer_ids.empty(); }
};
