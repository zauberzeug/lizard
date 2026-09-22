#pragma once

#include "module.h"
#include <atomic>
#include <cstdint>
#include <string>

// Console transport over ESP-NOW between Lizard nodes; a linked dongle forwards its UART0 to a peer (see module reference)
class EspNowBridge;
using EspNowBridge_ptr = std::shared_ptr<EspNowBridge>;

class EspNowBridge : public Module {
public:
    static inline constexpr const char *TYPE = "EspNowBridge";
    static constexpr size_t MAX_NODE_NAME = 15;
    static constexpr size_t MAX_FRAME = 250; // ESP_NOW_MAX_DATA_LEN
    static constexpr size_t MAX_LINE = 2048; // longest console line Lizard handles in either direction
    static constexpr size_t MAX_PEERS = 8;
    static constexpr size_t REASSEMBLY_SLOTS = 2;

    struct Frame {
        uint8_t mac[6]; // sender on receive, destination on send
        uint8_t len;
        uint8_t data[MAX_FRAME];
    };

    EspNowBridge(const std::string name, const std::string node, uint8_t channel);

    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static const std::map<std::string, Variable_ptr> get_defaults();

private:
    struct Peer {
        char node[MAX_NODE_NAME + 1] = "";
        uint8_t mac[6] = {};
    };

    struct Reassembly {
        uint8_t mac[6] = {};
        char kind = 0;
        uint8_t seq = 0;
        uint8_t next_fragment = 0;
        bool broken = false;
        size_t len = 0;
        char line[MAX_LINE];
    };

    const std::string node;
    char link_target[MAX_NODE_NAME + 1] = ""; // dongle mode: node that receives every UART0 line
    char commander[MAX_NODE_NAME + 1] = "";   // node that sent the last command; receives our console
    Peer peers[MAX_PEERS];
    size_t next_peer = 0;
    Reassembly slots[REASSEMBLY_SLOTS];
    uint8_t tx_seq = 0;
    unsigned long last_hello_ms = 0;

    void start_radio(uint8_t channel);
    bool intercept_uart0(const char *line, int len);
    void handle_frame(const Frame &frame);
    void handle_line(const char *from, char kind, const char *line, size_t len);
    void send_line(const char *to, char kind, const char *line, size_t len);
    void send_hello(const char *to);
    const uint8_t *learn_peer(const char *node, const uint8_t *mac);
    const uint8_t *find_peer(const char *node) const;
    Reassembly *slot_for(const uint8_t *mac, char kind, bool start);
};
