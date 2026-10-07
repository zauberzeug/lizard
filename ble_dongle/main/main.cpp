// Transparent Bluetooth dongle for the console of a robot running Lizard (see README.md):
// the host's bytes go to the robot's BLE console as they arrive, and the robot's console comes back byte for byte.
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_store.h>
#include <host/util/util.h>
#include <nimble/nimble_npl.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <os/os_mbuf.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

// Undef NimBLE macros that conflict with STL
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>

extern "C" void ble_store_config_init(void);

namespace {

constexpr uart_port_t UART = UART_NUM_0;
constexpr int UART_RX_BUFFER = 65536; // holds the host's bytes while the robot or the radio cannot keep up
constexpr int UART_TX_BUFFER = 8192;
constexpr size_t MAX_NAME = 29;
constexpr size_t MAX_COMMAND = 96;
constexpr int MIN_FREE_MBUFS = 12;     // keep buffers for the robot's console while writing
constexpr int32_t WINDOW_BYTES = 6144; // in flight to the robot, below its queue of 8 KB and 64 lines
constexpr int32_t WINDOW_LINES = 48;
constexpr int32_t SCAN_MS = 10000;
constexpr int32_t CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t RECONNECT_MS = 500;
constexpr uint32_t RETRY_AFTER_FAILURE_MS = 5000;
constexpr char PREFIX[] = "!dongle";
constexpr size_t PREFIX_LEN = sizeof(PREFIX) - 1;

ble_uuid_any_t svc_uuid;
ble_uuid_any_t console_in_uuid;
ble_uuid_any_t console_out_uuid;
ble_uuid_any_t console_flow_uuid;
QueueHandle_t uart_events;

// --- output to the host: the robot's bytes and own status lines, never mixed within a line

SemaphoreHandle_t uart_mutex;
bool line_open = false;    // the robot's current line has not ended yet
std::string held_messages; // own lines wait for the end of the robot's line
std::atomic<uint32_t> dropped_bytes{0};
std::atomic<uint32_t> receive_overflows{0};

void write_robot(const uint8_t *data, size_t len) {
    if (len == 0) {
        return;
    }
    xSemaphoreTake(uart_mutex, portMAX_DELAY);
    size_t free_space = 0;
    uart_get_tx_buffer_free_size(UART, &free_space);
    if (free_space < 2 * len) {
        dropped_bytes += len; // the host notices the broken line by its checksum
    } else {
        size_t start = 0;
        for (size_t i = 0; i < len; i++) {
            if (data[i] == '\n') { // CRLF like Lizard's UART0
                uart_write_bytes(UART, data + start, i - start);
                uart_write_bytes(UART, "\r\n", 2);
                start = i + 1;
            }
        }
        uart_write_bytes(UART, data + start, len - start);
        line_open = data[len - 1] != '\n';
    }
    if (!line_open && !held_messages.empty()) {
        uart_write_bytes(UART, held_messages.data(), held_messages.size());
        held_messages.clear();
    }
    xSemaphoreGive(uart_mutex);
}

void close_line() {
    xSemaphoreTake(uart_mutex, portMAX_DELAY);
    if (line_open) {
        uart_write_bytes(UART, "\r\n", 2); // a line cut by a lost link fails the host's checksum check
        line_open = false;
    }
    if (!held_messages.empty()) {
        uart_write_bytes(UART, held_messages.data(), held_messages.size());
        held_messages.clear();
    }
    xSemaphoreGive(uart_mutex);
}

// a status line with a checksum like Lizard's console lines
void say(const char *format, ...) {
    char text[160];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(text, sizeof(text) - 6, format, args);
    va_end(args);
    if (len < 0) {
        return;
    }
    len = std::min<int>(len, sizeof(text) - 7);
    uint8_t checksum = 0;
    for (int i = 0; i < len; i++) {
        checksum ^= text[i];
    }
    len += snprintf(text + len, 6, "@%02x\r\n", checksum);
    xSemaphoreTake(uart_mutex, portMAX_DELAY);
    if (line_open) {
        held_messages.append(text, len);
    } else {
        uart_write_bytes(UART, text, len);
    }
    xSemaphoreGive(uart_mutex);
}

// --- configuration, written by the UART task and stored in NVS

SemaphoreHandle_t config_mutex;
char config_target[MAX_NAME + 1] = "";
uint32_t config_pin = CONFIG_ZZ_BLE_DEV_PIN;
uint32_t config_baud = CONFIG_DONGLE_BAUD_RATE;

void load_config() {
    nvs_handle_t handle;
    if (nvs_open("dongle", NVS_READONLY, &handle) == ESP_OK) {
        size_t len = sizeof(config_target);
        nvs_get_str(handle, "target", config_target, &len);
        nvs_get_u32(handle, "pin", &config_pin);
        nvs_get_u32(handle, "baud", &config_baud);
        nvs_close(handle);
    }
}

void save_config() {
    nvs_handle_t handle;
    if (nvs_open("dongle", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, "target", config_target);
        nvs_set_u32(handle, "pin", config_pin);
        nvs_set_u32(handle, "baud", config_baud);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

// --- the link to the robot; its state only changes on the NimBLE host task

enum class LinkState {
    IDLE,
    SCANNING,
    CONNECTING,
    CONNECTED,
};

ble_npl_event request_event;
ble_npl_callout scan_callout;
LinkState link_state = LinkState::IDLE;
char target[MAX_NAME + 1] = "";
uint32_t pin = 0;
bool synced = false;
bool backoff = false; // the last attempt failed: wait longer before the next one
uint8_t own_addr_type = 0;
uint16_t service_start = 0;
uint16_t service_end = 0;
uint16_t console_out_handle = 0;
uint16_t console_flow_handle = 0;
std::atomic<uint16_t> conn_handle{BLE_HS_CONN_HANDLE_NONE};
std::atomic<uint16_t> console_in_handle{0};
std::atomic<bool> linked{false};

// flow control: the robot reports the input bytes and lines it has processed since the connection began
std::atomic<bool> flow_control{false};
std::atomic<bool> new_connection{false};
std::atomic<uint32_t> released_bytes{0};
std::atomic<uint32_t> released_lines{0};

int on_gap_event(ble_gap_event *event, void *arg);

void schedule_scan(uint32_t ms) {
    ble_npl_callout_reset(&scan_callout, ble_npl_time_ms_to_ticks32(ms));
}

void start_scan() {
    if (!synced || target[0] == '\0' || link_state != LinkState::IDLE) {
        return;
    }
    ble_gap_disc_params params = {};
    params.filter_duplicates = 1; // active scan: the robot sends its name in the scan response
    if (ble_gap_disc(own_addr_type, SCAN_MS, &params, on_gap_event, nullptr) == 0) {
        link_state = LinkState::SCANNING;
    } else {
        schedule_scan(RECONNECT_MS);
    }
}

void on_scan_callout(ble_npl_event *) {
    start_scan();
}

void stop_link() {
    switch (link_state) {
    case LinkState::SCANNING:
        ble_gap_disc_cancel();
        link_state = LinkState::IDLE;
        break;
    case LinkState::CONNECTING:
        ble_gap_conn_cancel(); // the failed connect event schedules the next scan
        break;
    case LinkState::CONNECTED:
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM); // the disconnect event schedules the next scan
        break;
    case LinkState::IDLE:
        break;
    }
}

void on_request(ble_npl_event *) {
    char new_target[MAX_NAME + 1];
    uint32_t new_pin;
    xSemaphoreTake(config_mutex, portMAX_DELAY);
    strcpy(new_target, config_target);
    new_pin = config_pin;
    xSemaphoreGive(config_mutex);
    if (strcmp(new_target, target) == 0 && new_pin == pin) {
        return;
    }
    strcpy(target, new_target);
    pin = new_pin;
    backoff = false;
    ble_npl_callout_stop(&scan_callout);
    stop_link();
    start_scan();
}

void drop_connection(uint16_t conn) {
    backoff = true;
    ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
}

int on_flow_subscribed(uint16_t conn, const ble_gatt_error *error, ble_gatt_attr *, void *) {
    if (error->status == 0) {
        flow_control = true;
        linked = true;
        say("dongle: linked to \"%s\"", target);
    } else {
        drop_connection(conn);
    }
    return 0;
}

int on_subscribed(uint16_t conn, const ble_gatt_error *error, ble_gatt_attr *, void *) {
    static const uint8_t enable_notifications[2] = {1, 0};
    if (error->status != 0) {
        drop_connection(conn);
    } else if (console_flow_handle != 0) { // robots with flow control tell how much more they can queue
        if (ble_gattc_write_flat(conn, console_flow_handle + 1, enable_notifications, sizeof(enable_notifications),
                                 on_flow_subscribed, nullptr) != 0) {
            drop_connection(conn);
        }
    } else {
        linked = true;
        say("dongle: linked to \"%s\"", target);
    }
    return 0;
}

int on_characteristic(uint16_t conn, const ble_gatt_error *error, const ble_gatt_chr *chr, void *) {
    if (error->status == 0) {
        if (ble_uuid_cmp(&chr->uuid.u, &console_in_uuid.u) == 0) {
            console_in_handle = chr->val_handle;
        } else if (ble_uuid_cmp(&chr->uuid.u, &console_out_uuid.u) == 0) {
            console_out_handle = chr->val_handle;
        } else if (ble_uuid_cmp(&chr->uuid.u, &console_flow_uuid.u) == 0) {
            console_flow_handle = chr->val_handle;
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE && console_in_handle != 0 && console_out_handle != 0) {
        // NimBLE places the client configuration descriptor right after the value of a notifying characteristic
        static const uint8_t enable_notifications[2] = {1, 0};
        if (ble_gattc_write_flat(conn, console_out_handle + 1, enable_notifications, sizeof(enable_notifications),
                                 on_subscribed, nullptr) == 0) {
            return 0;
        }
    } else if (error->status == BLE_HS_EDONE) {
        say("dongle: \"%s\" offers no Bluetooth console (Lizard too old?)", target);
    }
    drop_connection(conn);
    return 0;
}

int on_service(uint16_t conn, const ble_gatt_error *error, const ble_gatt_svc *service, void *) {
    if (error->status == 0) {
        service_start = service->start_handle;
        service_end = service->end_handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE && service_end != 0 &&
        ble_gattc_disc_all_chrs(conn, service_start, service_end, on_characteristic, nullptr) == 0) {
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        say("dongle: \"%s\" offers no Bluetooth console (Lizard too old?)", target);
    }
    drop_connection(conn);
    return 0;
}

int on_mtu(uint16_t, const ble_gatt_error *, uint16_t, void *) {
    return 0;
}

int on_gap_event(ble_gap_event *event, void *) {
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        if (link_state != LinkState::SCANNING) {
            return 0;
        }
        ble_hs_adv_fields fields;
        const size_t target_len = strlen(target);
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0 || fields.name == nullptr ||
            fields.name_len != target_len || memcmp(fields.name, target, target_len) != 0) {
            return 0;
        }
        ble_gap_disc_cancel();
        ble_gap_conn_params params = {};
        params.scan_itvl = 0x10;
        params.scan_window = 0x10;
        params.itvl_min = 6; // 7.5 ms, the shortest interval
        params.itvl_max = 6;
        params.latency = 0;
        params.supervision_timeout = 200; // 2 s
        if (ble_gap_connect(own_addr_type, &event->disc.addr, CONNECT_TIMEOUT_MS, &params, on_gap_event, nullptr) == 0) {
            link_state = LinkState::CONNECTING;
        } else {
            link_state = LinkState::IDLE;
            schedule_scan(RECONNECT_MS);
        }
        return 0;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (link_state == LinkState::SCANNING) {
            link_state = LinkState::IDLE;
            start_scan(); // keep looking until the robot shows up
        }
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            link_state = LinkState::IDLE;
            schedule_scan(RECONNECT_MS);
            return 0;
        }
        link_state = LinkState::CONNECTED;
        conn_handle = event->connect.conn_handle;
        console_in_handle = 0;
        console_out_handle = 0;
        console_flow_handle = 0;
        flow_control = false;
        released_bytes = 0;
        released_lines = 0;
        new_connection = true;
        service_start = 0;
        service_end = 0;
        ble_gap_set_data_len(event->connect.conn_handle, 0xFB, 0x0848);
        ble_gattc_exchange_mtu(event->connect.conn_handle, on_mtu, nullptr);
        ble_gap_security_initiate(event->connect.conn_handle); // encrypts with a stored bond or pairs with the PIN
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
            ble_sm_io io = {};
            io.action = BLE_SM_IOACT_INPUT;
            io.passkey = pin;
            ble_sm_inject_io(event->passkey.conn_handle, &io);
        }
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            if (ble_gattc_disc_svc_by_uuid(event->enc_change.conn_handle, &svc_uuid.u, on_service, nullptr) != 0) {
                drop_connection(event->enc_change.conn_handle);
            }
        } else {
            // a wrong PIN or a bond the robot no longer knows: forget ours, so that the next attempt pairs again
            ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }
            say("dongle: could not pair with \"%s\", check the PIN", target);
            drop_connection(event->enc_change.conn_handle);
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.attr_handle == console_out_handle) {
            for (const os_mbuf *m = event->notify_rx.om; m != nullptr; m = SLIST_NEXT(m, om_next)) {
                write_robot(m->om_data, m->om_len);
            }
        } else if (event->notify_rx.attr_handle == console_flow_handle) {
            uint8_t data[8];
            if (os_mbuf_copydata(event->notify_rx.om, 0, sizeof(data), data) == 0) {
                released_bytes = data[0] | data[1] << 8 | data[2] << 16 | static_cast<uint32_t>(data[3]) << 24;
                released_lines = data[4] | data[5] << 8 | data[6] << 16 | static_cast<uint32_t>(data[7]) << 24;
            }
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT: {
        const bool was_linked = linked;
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        linked = false;
        link_state = LinkState::IDLE;
        if (was_linked) {
            close_line();
            say("dongle: link to \"%s\" closed", target);
        }
        schedule_scan(backoff ? RETRY_AFTER_FAILURE_MS : RECONNECT_MS);
        backoff = false;
        return 0;
    }

    default:
        return 0;
    }
}

void on_sync() {
    if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        return;
    }
    synced = true;
    start_scan();
}

void on_reset(int) {
    synced = false;
    linked = false;
    conn_handle = BLE_HS_CONN_HANDLE_NONE;
    link_state = LinkState::IDLE;
}

void host_task(void *) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// --- input from the host

// writes host bytes to the robot right away, waiting only while the radio is out of buffers
// or, with flow control, while the robot has not processed enough of what is in flight
void forward(const uint8_t *data, size_t len) {
    static uint32_t sent_bytes = 0;
    static uint32_t sent_lines = 0;
    while (len > 0) {
        if (!linked) {
            dropped_bytes += len;
            return;
        }
        if (new_connection.exchange(false)) {
            sent_bytes = 0;
            sent_lines = 0;
        }
        const uint16_t mtu = ble_att_mtu(conn_handle);
        size_t chunk = std::min<size_t>(len, mtu > 3 ? mtu - 3 : 20);
        uint32_t lines = 0;
        if (flow_control) {
            // signed, because the robot may still release lines of the previous connection
            const int32_t bytes_in_flight = std::max<int32_t>(0, static_cast<int32_t>(sent_bytes - released_bytes));
            const int32_t lines_in_flight = std::max<int32_t>(0, static_cast<int32_t>(sent_lines - released_lines));
            const uint32_t free_bytes = std::max<int32_t>(0, WINDOW_BYTES - bytes_in_flight);
            const uint32_t free_lines = std::max<int32_t>(0, WINDOW_LINES - lines_in_flight);
            size_t n = 0;
            for (; n < std::min<size_t>(chunk, free_bytes); n++) {
                if (data[n] == '\n') {
                    if (lines == free_lines) {
                        break;
                    }
                    lines++;
                }
            }
            chunk = n;
            if (chunk == 0) {
                vTaskDelay(1); // until the robot reports progress
                continue;
            }
        }
        const int rc = os_msys_num_free() < MIN_FREE_MBUFS ? BLE_HS_ENOMEM
                                                           : ble_gattc_write_no_rsp_flat(conn_handle, console_in_handle, data, chunk);
        if (rc == BLE_HS_ENOMEM) {
            vTaskDelay(1);
            continue;
        }
        if (rc != 0) {
            dropped_bytes += chunk;
        } else {
            sent_bytes += chunk;
            sent_lines += lines;
        }
        data += chunk;
        len -= chunk;
    }
}

void handle_command(char *line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ')) {
        line[--len] = '\0';
    }
    if (len >= 3 && line[len - 3] == '@') { // a checksum as Lizard's host tools add it
        uint8_t checksum = 0;
        for (size_t i = 0; i < len - 3; i++) {
            checksum ^= line[i];
        }
        if (strtol(line + len - 2, nullptr, 16) != checksum) {
            say("dongle: checksum mismatch");
            return;
        }
        line[len - 3] = '\0';
    }
    char *args = line + PREFIX_LEN;
    while (*args == ' ') {
        args++;
    }
    if (strncmp(args, "link ", 5) == 0) {
        char *name = args + 5;
        while (*name == ' ') {
            name++;
        }
        char *rest;
        if (*name == '"') { // a quoted name may contain spaces
            name++;
            rest = strchr(name, '"');
        } else {
            rest = strchr(name, ' ');
        }
        if (rest != nullptr) {
            *rest++ = '\0';
        }
        const long new_pin = rest != nullptr && *rest != '\0' ? strtol(rest, nullptr, 10) : CONFIG_ZZ_BLE_DEV_PIN;
        if (*name == '\0' || strlen(name) > MAX_NAME || new_pin < 0 || new_pin > 999999) {
            say("dongle: usage: !dongle link <device name> [pin]");
            return;
        }
        xSemaphoreTake(config_mutex, portMAX_DELAY);
        strcpy(config_target, name);
        config_pin = new_pin;
        xSemaphoreGive(config_mutex);
        save_config();
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &request_event);
        say("dongle: linking to \"%s\"", name);
    } else if (strncmp(args, "baud ", 5) == 0) {
        const long baud = strtol(args + 5, nullptr, 10);
        if (baud != 115200 && baud != 230400 && baud != 460800 && baud != 921600) {
            say("dongle: supported baud rates are 115200, 230400, 460800 and 921600");
            return;
        }
        config_baud = baud;
        save_config();
        say("dongle: switching to %ld baud", baud); // still at the old rate
        uart_wait_tx_done(UART, pdMS_TO_TICKS(100));
        uart_set_baudrate(UART, baud);
    } else if (strcmp(args, "unlink") == 0) {
        xSemaphoreTake(config_mutex, portMAX_DELAY);
        config_target[0] = '\0';
        xSemaphoreGive(config_mutex);
        save_config();
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &request_event);
        say("dongle: unlinked");
    } else if (*args == '\0') {
        xSemaphoreTake(config_mutex, portMAX_DELAY);
        char name[MAX_NAME + 1];
        strcpy(name, config_target);
        xSemaphoreGive(config_mutex);
        if (name[0] == '\0') {
            say("dongle: no robot, use !dongle link <device name> [pin]");
        } else {
            say("dongle: %s \"%s\"%s, %lu baud, %lu bytes dropped, %lu receive overflows", linked ? "linked to" : "looking for",
                name, flow_control ? " with flow control" : "", static_cast<unsigned long>(config_baud),
                static_cast<unsigned long>(dropped_bytes.load()), static_cast<unsigned long>(receive_overflows.load()));
        }
    } else {
        say("dongle: usage: !dongle [link <device name> [pin] | unlink | baud <rate>]");
    }
}

// every byte goes to the robot as it arrives, except lines that start with "!dongle"
void input(const uint8_t *data, size_t len) {
    enum class Mode { FORWARD,
                      PREFIX,
                      COMMAND };
    static Mode mode = Mode::FORWARD;
    static bool line_start = true;
    static char command[MAX_COMMAND + 1];
    static size_t command_len = 0;
    size_t start = 0; // first byte not forwarded yet
    for (size_t i = 0; i < len; i++) {
        const char c = data[i];
        if (mode == Mode::FORWARD) {
            if (line_start && c == '!') {
                forward(data + start, i - start); // a line for the dongle or a "!" command for Lizard
                mode = Mode::PREFIX;
                command[0] = c;
                command_len = 1;
                start = i + 1;
            }
            line_start = c == '\n';
            continue;
        }
        start = i + 1;
        if (mode == Mode::PREFIX) {
            command[command_len++] = c;
            if (c != PREFIX[command_len - 1]) {
                forward(reinterpret_cast<const uint8_t *>(command), command_len); // not for the dongle after all
                mode = Mode::FORWARD;
                line_start = c == '\n';
            } else if (command_len == PREFIX_LEN) {
                mode = Mode::COMMAND;
            }
        } else if (c == '\n') {
            command[std::min(command_len, MAX_COMMAND)] = '\0';
            handle_command(command);
            mode = Mode::FORWARD;
            line_start = true;
        } else if (command_len < MAX_COMMAND) {
            command[command_len++] = c;
        }
    }
    if (mode == Mode::FORWARD) {
        forward(data + start, len - start);
    }
}

void uart_task(void *) {
    uint8_t buffer[512];
    uart_event_t event;
    while (true) {
        if (xQueueReceive(uart_events, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
            receive_overflows++; // only the bytes that did not fit are lost, the buffered ones are still valid
        }
        size_t buffered = 0;
        uart_get_buffered_data_len(UART, &buffered);
        while (buffered > 0) {
            const int read = uart_read_bytes(UART, buffer, std::min(buffered, sizeof(buffer)), 0);
            if (read <= 0) {
                break;
            }
            input(buffer, read);
            buffered -= read;
        }
    }
}

} // namespace

extern "C" void app_main() {
    uart_mutex = xSemaphoreCreateMutex();
    config_mutex = xSemaphoreCreateMutex();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    load_config();

    uart_config_t uart_config = {};
    uart_config.baud_rate = config_baud;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_driver_install(UART, UART_RX_BUFFER, UART_TX_BUFFER, 32, &uart_events, 0));
    ESP_ERROR_CHECK(uart_param_config(UART, &uart_config));
    uart_set_rx_timeout(UART, 2); // hand over bytes after 2 idle symbols instead of 10

    ble_uuid_from_str(&svc_uuid, CONFIG_DONGLE_SVC_UUID);
    ble_uuid_from_str(&console_in_uuid, CONFIG_DONGLE_CONSOLE_IN_UUID);
    ble_uuid_from_str(&console_out_uuid, CONFIG_DONGLE_CONSOLE_OUT_UUID);
    ble_uuid_from_str(&console_flow_uuid, CONFIG_DONGLE_CONSOLE_FLOW_UUID);
    ESP_ERROR_CHECK(nimble_port_init());
    ble_store_config_init();
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_KEYBOARD_ONLY; // the robot shows its PIN, the dongle enters it
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("Lizard BLE dongle");
    ble_npl_event_init(&request_event, on_request, nullptr);
    ble_npl_callout_init(&scan_callout, nimble_port_get_dflt_eventq(), on_scan_callout, nullptr);
    nimble_port_freertos_init(host_task);

    xTaskCreate(uart_task, "uart", 4096, nullptr, 10, nullptr);
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &request_event); // link to the stored robot
    if (config_target[0] == '\0') {
        say("dongle: no robot, use !dongle link <device name> [pin]");
    } else {
        say("dongle: looking for \"%s\"", config_target);
    }
}
