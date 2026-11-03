#pragma once

#include "../utils/otb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "module.h"
#include "serial.h"
#include <atomic>
#include <bitset>
#include <cstdint>
#include <map>
#include <vector>

class SerialBus;
using SerialBus_ptr = std::shared_ptr<SerialBus>;
class BusTelemetry;

class SerialBus : public Module {
public:
    static inline constexpr const char *TYPE = "SerialBus";

    static constexpr size_t PAYLOAD_CAPACITY = 256;

    const ConstSerial_ptr serial;
    const uint8_t node_id;

    SerialBus(const std::string &name, const ConstSerial_ptr serial, const uint8_t node_id);
    ~SerialBus() override;

    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();
    static uint8_t to_peer_id(const ConstExpression_ptr &argument);

    // --- telemetry frames (see utils/telemetry.h) ---------------------------
    // peer side, main task: the node that polls this one, 0 before the first poll
    uint8_t coordinator() const { return this->coordinator_id; }
    bool send_telemetry_layout(const char *line, const size_t length); // false while only the queue's reserve is free
    bool telemetry_frame_pending(const uint8_t frame_id);
    // keeps the newest frame of an ID for the next poll; with `replace` only while the previous one has not left yet
    bool store_telemetry_frame(const uint8_t frame_id, const char *line, const size_t length, const bool replace);
    void clear_telemetry_slots();
    // coordinator side
    void request_passed_layouts(); // asks the peers whose frames are passed on for their layout lines
    void add_telemetry_listener(BusTelemetry *listener);
    void remove_telemetry_listener(BusTelemetry *listener);
    const BusTelemetry *find_telemetry_listener(const uint8_t peer_id, const std::string &property_name) const; // the one declaring it
    // the peer's next frame asks for its layout lines again, while the layouts held so far stay in use
    void refresh_telemetry_layout(const uint8_t peer_id);

protected:
    static constexpr unsigned long PEER_BOOT_MS = 5000; // a restarted peer answers within this

    // a bus that also claims `pins`, e.g. those wired to its peer, and keeps fewer messages to send
    SerialBus(const std::string &name, const ConstSerial_ptr serial, const uint8_t node_id,
              const std::initializer_list<gpio_num_t> pins, const size_t outgoing_queue_length);

    void set_peers(const uint8_t *peer_ids, const uint8_t peer_count); // makes this node the coordinator
    void expect_peer_boot();                                           // poll timeouts are not reported for PEER_BOOT_MS
    void pause_communication();                                        // so that the main task can use the UART
    void resume_communication() { this->pause_requested = false; }

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
        uint8_t peer_ids[254]; // one slot per valid ID (1..254)
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
    const size_t outgoing_queue_length; // too short for an OTB update if not longer than its window
    // written by the communication task, drained and reported by step() on the main task
    std::atomic<unsigned> dropped_inbound{0};
    unsigned long last_drop_report_millis = 0;
    TaskHandle_t communication_task = nullptr;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> stopped{false};
    std::atomic<bool> pause_requested{false}; // the main task uses the UART, e.g. to flash the peer
    std::atomic<bool> paused{false};
    std::atomic<bool> peer_booting{false};
    std::atomic<unsigned long> peer_boot_millis{0};
    int echo_callback_handle = 0;
    std::atomic<unsigned> dropped_output{0}; // console lines of this node that found no place in the outbound queue
    unsigned long last_output_drop_report_millis = 0;
    std::atomic<unsigned> ignored_lines{0}; // counted by the communication task
    Variable_ptr ignored_lines_property;

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
    // frame buffers, kept off the task's stack and not shared with the tasks of other buses
    static constexpr size_t RX_FRAME_SIZE = 512;                   // any line of a peer; longer ones are discarded
    static constexpr size_t TX_FRAME_SIZE = 16 + PAYLOAD_CAPACITY; // the header and a payload below PAYLOAD_CAPACITY
    static_assert(sizeof("$$255:255$$") <= 16, "the longest bus header must fit the room before the payload");
    char rx_frame[RX_FRAME_SIZE];
    char tx_frame[TX_FRAME_SIZE];

    // --- telemetry, peer side ----------------------------------------------
    // The main task leaves the newest frame of each ID in a slot and the communication task sends the slots with the
    // next poll, so a poll delivers current values and no backlog. The frame ID is only touched by the main task.
    struct FrameSlot {
        uint8_t frame_id = 0; // 0: free
        bool pending = false; // stored and not sent yet
        uint16_t length = 0;
        char line[PAYLOAD_CAPACITY];
    };
    static constexpr size_t FRAME_SLOTS = 8;
    std::atomic<FrameSlot *> frame_slots{nullptr}; // allocated by the main task with the first frame
    portMUX_TYPE frame_slot_lock = portMUX_INITIALIZER_UNLOCKED;
    std::atomic<uint8_t> coordinator_id{0};
    bool slot_warning_sent = false; // the first warning comes right away, not only 10 s after boot
    unsigned long last_slot_warning_millis = 0;
    char slot_line[PAYLOAD_CAPACITY];                         // the frame being sent, copied out of its slot
    FrameSlot *find_frame_slot(const uint8_t frame_id) const; // 0 finds a free one
    void send_frame_slots(const uint8_t receiver);

    // --- telemetry, coordinator side (main task) ---------------------------
    struct FrameLayout {
        uint8_t layout_id = 0;
        std::vector<char> types; // type letter per field index, 0 until its layout line arrived
    };
    struct PeerLayout {
        std::map<uint8_t, FrameLayout> frames; // by frame ID
        bool other_version = false;            // the peer sends a format this node does not read, until its Ready.
        bool requested = false;
        bool awaiting = false; // requested, but no layout line arrived yet
        bool stale = false;    // the layout lines are needed again, e.g. for a new declaration, however complete the layouts are
        unsigned long request_millis = 0;
    };
    std::bitset<256> passed_peers;
    std::vector<BusTelemetry *> telemetry_listeners;
    std::map<uint8_t, PeerLayout> peer_layouts; // only of peers whose frames are passed on or listened to
    std::atomic<unsigned> dropped_frames{0};    // counted by the communication task
    Variable_ptr telemetry_errors;
    Variable_ptr telemetry_dropped;
    bool passes_telemetry(const uint8_t peer_id) const { return this->passed_peers[peer_id]; }
    bool is_listened_to(const uint8_t peer_id) const;
    void handle_telemetry_frame(const IncomingMessage &message);
    void handle_telemetry_layout(const IncomingMessage &message);
    void request_layout(const uint8_t peer_id, PeerLayout &layout);
    static void refresh_layout(PeerLayout &layout);

    static void communication_loop(void *param);
    void adopt_config(const Config &config);
    void process_uart();
    void push_incoming(const IncomingMessage &message);
    bool parse_message(const char *message_line, IncomingMessage &message) const;
    void handle_incoming_message(const IncomingMessage &message);
    void enqueue_outgoing_message(const uint8_t receiver, const char *payload, const size_t length);
    // without the checks of enqueue_outgoing_message; false if the queue stays full for `wait`
    bool queue_outgoing_message(const uint8_t receiver, const char *payload, const size_t length, const TickType_t wait);
    bool send_outgoing_queue();
    size_t send_message(const uint8_t receiver, const char *payload, const size_t length);

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
    void relay_console_line(const char *line);
    bool is_coordinator() const { return !this->peer_ids.empty(); }
};
