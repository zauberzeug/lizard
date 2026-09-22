#include "espnow_bridge.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "../main.h"
#include "nvs_flash.h"
#include "../utils/uart.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

// Frame: 'E' 'N' version kind seq fragment flags | to NUL | from NUL | payload. Radio state is process-global because
// esp_now initializes once; the callbacks run in the WiFi task and only move frames through queues, step() handles them
// on the main task where process_line and echo are safe.
static constexpr size_t HEADER_SIZE = 7;
static constexpr uint8_t PROTOCOL_VERSION = 2;
static constexpr uint8_t FLAG_LAST = 1;
static constexpr size_t RX_QUEUE_LENGTH = 16;
static constexpr size_t TX_QUEUE_LENGTH = 24;
static constexpr unsigned long HELLO_INTERVAL_MS = 1000;
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static EspNowBridge *active_bridge = nullptr;
static QueueHandle_t rx_queue = nullptr;
static QueueHandle_t tx_queue = nullptr;
static SemaphoreHandle_t tx_done = nullptr;
static std::atomic<uint32_t> tx_frames{0};
static std::atomic<uint32_t> lost_frames{0};
static bool printing_remote_echo = false; // a received console line is printed, never forwarded again

static void recv_handler(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (rx_queue == nullptr || len < (int)HEADER_SIZE || len > (int)EspNowBridge::MAX_FRAME) {
        return;
    }
    if (data[0] != 'E' || data[1] != 'N' || data[2] != PROTOCOL_VERSION) {
        return; // foreign ESP-NOW traffic
    }
    EspNowBridge::Frame frame;
    memcpy(frame.mac, info->src_addr, 6);
    frame.len = len;
    memcpy(frame.data, data, len);
    xQueueSend(rx_queue, &frame, 0); // full queue: drop, the receiver detects the missing fragment
}

static void send_handler(const uint8_t *, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        lost_frames.fetch_add(1, std::memory_order_relaxed);
    }
    xSemaphoreGive(tx_done);
}

// one frame in flight at a time; the radio acknowledges unicast frames and reports the result through send_handler
static void tx_task(void *) {
    EspNowBridge::Frame frame;
    while (true) {
        if (xQueueReceive(tx_queue, &frame, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (esp_now_send(frame.mac, frame.data, frame.len) == ESP_OK) {
            tx_frames.fetch_add(1, std::memory_order_relaxed);
            xSemaphoreTake(tx_done, pdMS_TO_TICKS(100));
        } else {
            lost_frames.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

static void require(esp_err_t err, const char *what) {
    if (err != ESP_OK) {
        throw std::runtime_error(std::string(what) + " failed: " + esp_err_to_name(err));
    }
}

static void validate_node_name(const std::string &node) {
    if (node.empty() || node.size() > EspNowBridge::MAX_NODE_NAME || node == "*") {
        throw std::runtime_error("node name must be 1..15 characters and not \"*\"");
    }
}

const std::map<std::string, Variable_ptr> EspNowBridge::get_defaults() {
    return {
        {"node", std::make_shared<StringVariable>("")},
        {"link", std::make_shared<StringVariable>("")},
        {"rx", std::make_shared<IntegerVariable>(0)},
        {"tx", std::make_shared<IntegerVariable>(0)},
        {"lost", std::make_shared<IntegerVariable>(0)},
    };
}

EspNowBridge::EspNowBridge(const std::string name, const std::string node, uint8_t channel)
    : Module(name), node(node) {
    validate_node_name(node);
    if (active_bridge != nullptr) {
        throw std::runtime_error("node \"" + active_bridge->node + "\" already owns the radio");
    }
    this->properties = EspNowBridge::get_defaults();
    this->get_property("node")->string_value = node;
    this->start_radio(channel);

    active_bridge = this;
    register_echo_callback([](const char *line) {
        if (printing_remote_echo || active_bridge->commander[0] == '\0') {
            return;
        }
        active_bridge->send_line(active_bridge->commander, 'e', line, strlen(line));
    });
    set_uart0_interceptor([](const char *line, int len) { return active_bridge->intercept_uart0(line, len); });
    this->send_hello("*");
}

void EspNowBridge::start_radio(uint8_t channel) {
    if (channel < 1 || channel > 13) {
        throw std::runtime_error("channel must be 1..13");
    }
    rx_queue = xQueueCreate(RX_QUEUE_LENGTH, sizeof(Frame));
    tx_queue = xQueueCreate(TX_QUEUE_LENGTH, sizeof(Frame));
    tx_done = xSemaphoreCreateBinary();
    if (rx_queue == nullptr || tx_queue == nullptr || tx_done == nullptr) {
        throw std::runtime_error("could not allocate ESP-NOW queues");
    }
    esp_err_t err = nvs_flash_init(); // usually done by Lizard already
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        require(err, "nvs_flash_init");
    }
    require(esp_netif_init(), "esp_netif_init");
    err = esp_event_loop_create_default();
    if (err != ESP_ERR_INVALID_STATE) {
        require(err, "esp_event_loop_create_default");
    }
    for (const char *tag : {"wifi", "wifi_init", "phy_init", "ESPNOW"}) {
        esp_log_level_set(tag, ESP_LOG_WARN); // keep the radio's init chatter off the console
    }
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    require(esp_wifi_init(&init_config), "esp_wifi_init");
    require(esp_wifi_set_storage(WIFI_STORAGE_RAM), "esp_wifi_set_storage");
    require(esp_wifi_set_mode(WIFI_MODE_STA), "esp_wifi_set_mode");
    require(esp_wifi_start(), "esp_wifi_start");
    require(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE), "esp_wifi_set_channel");
    esp_wifi_set_ps(WIFI_PS_NONE); // no modem sleep: every frame is received immediately
    require(esp_now_init(), "esp_now_init");
    require(esp_now_register_recv_cb(recv_handler), "esp_now_register_recv_cb");
    require(esp_now_register_send_cb(send_handler), "esp_now_register_send_cb");
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST_MAC, 6);
    peer.ifidx = WIFI_IF_STA;
    require(esp_now_add_peer(&peer), "esp_now_add_peer");
    if (xTaskCreate(tx_task, "espnow_tx", 3072, nullptr, 5, nullptr) != pdPASS) {
        throw std::runtime_error("could not start the ESP-NOW transmit task");
    }
}

bool EspNowBridge::intercept_uart0(const char *line, int len) {
    if (this->link_target[0] == '\0') {
        return false;
    }
    const size_t name_len = this->name.size();
    if ((size_t)len > name_len && strncmp(line, this->name.c_str(), name_len) == 0 && line[name_len] == '.') {
        return false; // the dongle's own methods stay local
    }
    this->send_line(this->link_target, 'c', line, len);
    return true;
}

void EspNowBridge::send_line(const char *to, char kind, const char *line, size_t len) {
    const size_t to_len = strlen(to);
    const size_t from_len = this->node.size();
    const size_t capacity = MAX_FRAME - HEADER_SIZE - (to_len + 1) - (from_len + 1);
    if (len >= MAX_LINE || to_len > MAX_NODE_NAME) {
        if (kind == 'c') { // console lines are never reported from the echo path
            echo("warning: %s dropped a line of %u bytes", this->name.c_str(), (unsigned)len);
        }
        return;
    }
    const uint8_t *mac = find_peer(to);
    if (mac == nullptr) {
        mac = BROADCAST_MAC; // "*" or a node we have not heard from yet
    }
    const uint8_t seq = this->tx_seq++;
    uint8_t fragment = 0;
    size_t offset = 0;
    do {
        const size_t chunk = std::min(capacity, len - offset);
        Frame frame;
        memcpy(frame.mac, mac, 6);
        frame.data[0] = 'E';
        frame.data[1] = 'N';
        frame.data[2] = PROTOCOL_VERSION;
        frame.data[3] = kind;
        frame.data[4] = seq;
        frame.data[5] = fragment;
        frame.data[6] = offset + chunk >= len ? FLAG_LAST : 0;
        size_t pos = HEADER_SIZE;
        memcpy(frame.data + pos, to, to_len + 1);
        pos += to_len + 1;
        memcpy(frame.data + pos, this->node.c_str(), from_len + 1);
        pos += from_len + 1;
        memcpy(frame.data + pos, line + offset, chunk);
        frame.len = pos + chunk;
        if (xQueueSend(tx_queue, &frame, 0) != pdTRUE) {
            lost_frames.fetch_add(1, std::memory_order_relaxed); // the receiver drops the incomplete line
            return;
        }
        offset += chunk;
        fragment++;
    } while (offset < len);
}

void EspNowBridge::send_hello(const char *to) {
    this->send_line(to, 'h', "", 0);
    this->last_hello_ms = esp_timer_get_time() / 1000;
}

const uint8_t *EspNowBridge::find_peer(const char *node) const {
    for (const Peer &peer : this->peers) {
        if (peer.node[0] != '\0' && strcmp(peer.node, node) == 0) {
            return peer.mac;
        }
    }
    return nullptr;
}

const uint8_t *EspNowBridge::learn_peer(const char *node, const uint8_t *mac) {
    Peer *peer = nullptr;
    for (Peer &candidate : this->peers) {
        if (candidate.node[0] != '\0' && strcmp(candidate.node, node) == 0) {
            peer = &candidate;
        }
    }
    if (peer != nullptr && memcmp(peer->mac, mac, 6) == 0) {
        return peer->mac;
    }
    if (peer == nullptr) {
        peer = &this->peers[this->next_peer];
        this->next_peer = (this->next_peer + 1) % MAX_PEERS;
    }
    if (peer->node[0] != '\0') {
        esp_now_del_peer(peer->mac); // replaced or re-flashed node
    }
    strncpy(peer->node, node, MAX_NODE_NAME);
    peer->node[MAX_NODE_NAME] = '\0';
    memcpy(peer->mac, mac, 6);
    esp_now_peer_info_t info = {};
    memcpy(info.peer_addr, mac, 6);
    info.ifidx = WIFI_IF_STA;
    const esp_err_t err = esp_now_add_peer(&info);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        echo("warning: %s could not register node \"%s\": %s", this->name.c_str(), node, esp_err_to_name(err));
    }
    return peer->mac;
}

EspNowBridge::Reassembly *EspNowBridge::slot_for(const uint8_t *mac, char kind, bool start) {
    for (Reassembly &slot : this->slots) {
        if (slot.kind == kind && memcmp(slot.mac, mac, 6) == 0) {
            return &slot;
        }
    }
    if (!start) {
        return nullptr; // fragment of a line whose start we missed
    }
    Reassembly *slot = &this->slots[0];
    for (Reassembly &candidate : this->slots) {
        if (candidate.kind == 0) {
            slot = &candidate;
            break;
        }
    }
    memcpy(slot->mac, mac, 6);
    slot->kind = kind;
    return slot;
}

void EspNowBridge::handle_frame(const Frame &frame) {
    const char kind = frame.data[3];
    const uint8_t seq = frame.data[4];
    const uint8_t fragment = frame.data[5];
    const uint8_t flags = frame.data[6];
    const char *to = (const char *)frame.data + HEADER_SIZE;
    const char *end = (const char *)frame.data + frame.len;
    const char *from = (const char *)memchr(to, '\0', end - to);
    if (from == nullptr) {
        return;
    }
    from++;
    const char *payload = (const char *)memchr(from, '\0', end - from);
    if (payload == nullptr) {
        return;
    }
    payload++;
    const size_t payload_len = end - payload;
    if (strcmp(to, this->node.c_str()) != 0 && strcmp(to, "*") != 0) {
        return; // addressed to another node
    }
    if (from[0] == '\0' || strlen(from) > MAX_NODE_NAME) {
        return;
    }
    this->get_property("rx")->integer_value++;
    this->learn_peer(from, frame.mac);
    if (kind == 'h') {
        if (strcmp(to, "*") == 0) {
            this->send_hello(from); // answer unicast so the caller learns our address
        }
        return;
    }
    Reassembly *slot = this->slot_for(frame.mac, kind, fragment == 0);
    if (slot == nullptr) {
        return;
    }
    if (fragment == 0) {
        slot->seq = seq;
        slot->next_fragment = 0;
        slot->len = 0;
        slot->broken = false;
    } else if (slot->seq != seq || slot->next_fragment != fragment) {
        slot->broken = true; // a fragment got lost: drop the whole line
    }
    if (!slot->broken) {
        if (slot->len + payload_len >= MAX_LINE) {
            slot->broken = true;
        } else {
            memcpy(slot->line + slot->len, payload, payload_len);
            slot->len += payload_len;
        }
    }
    slot->next_fragment = fragment + 1;
    if (flags & FLAG_LAST) {
        if (!slot->broken) {
            slot->line[slot->len] = '\0';
            this->handle_line(from, kind, slot->line, slot->len);
        }
        slot->kind = 0; // free the slot
    }
}

void EspNowBridge::handle_line(const char *from, char kind, const char *line, size_t len) {
    if (kind == 'c') {
        strncpy(this->commander, from, MAX_NODE_NAME); // this node's console now streams to the sender
        this->commander[MAX_NODE_NAME] = '\0';
        try {
            process_line(line, len);
        } catch (const std::exception &e) {
            echo("error: %s", e.what());
        }
    } else if (kind == 'e') {
        printing_remote_echo = true;
        if (this->link_target[0] != '\0' && strcmp(from, this->link_target) == 0) {
            echo("%s", line); // transparent: the linked node's console appears as our own
        } else {
            echo("[%s] %s", from, line);
        }
        printing_remote_echo = false;
    }
}

void EspNowBridge::step() {
    Frame frame;
    while (xQueueReceive(rx_queue, &frame, 0) == pdTRUE) {
        this->handle_frame(frame);
    }
    this->get_property("tx")->integer_value = tx_frames.load(std::memory_order_relaxed);
    this->get_property("lost")->integer_value = lost_frames.load(std::memory_order_relaxed);
    if (this->link_target[0] != '\0' && this->find_peer(this->link_target) == nullptr) {
        const unsigned long now_ms = esp_timer_get_time() / 1000;
        if (now_ms - this->last_hello_ms >= HELLO_INTERVAL_MS) {
            this->send_hello("*"); // keep looking for the linked node until it answers
        }
    }
    Module::step();
}

void EspNowBridge::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "send") {
        Module::expect(arguments, 2, string, string);
        const std::string target = arguments[0]->evaluate_string();
        const std::string line = arguments[1]->evaluate_string();
        this->send_line(target.c_str(), 'c', line.c_str(), line.size());
    } else if (method_name == "link") {
        Module::expect(arguments, 1, string);
        const std::string target = arguments[0]->evaluate_string();
        validate_node_name(target);
        if (target == this->node) {
            throw std::runtime_error("cannot link to this node itself");
        }
        strncpy(this->link_target, target.c_str(), MAX_NODE_NAME);
        this->link_target[MAX_NODE_NAME] = '\0';
        this->get_property("link")->string_value = target;
        this->send_hello("*");
    } else if (method_name == "unlink") {
        Module::expect(arguments, 0);
        this->link_target[0] = '\0';
        this->get_property("link")->string_value = "";
    } else if (method_name == "ping") { // clients measure their round-trip delay with this
        Module::expect(arguments, 0);
        echo("%s pong", this->name.c_str());
    } else {
        Module::call(method_name, arguments);
    }
}

static Module_ptr create_espnow_bridge(const std::string &name,
                                       const std::vector<ConstExpression_ptr> &arguments,
                                       MessageHandler) {
    if (arguments.size() < 1 || arguments.size() > 2) {
        throw std::runtime_error("expecting 1 or 2 arguments (node[, channel])");
    }
    Module::expect(arguments, -1, string, integer);
    const std::string node = arguments[0]->evaluate_string();
    const int channel = arguments.size() > 1 ? arguments[1]->evaluate_integer() : 1;
    return std::make_shared<EspNowBridge>(name, node, (uint8_t)channel);
}
REGISTER_MODULE(EspNowBridge, &create_espnow_bridge)
