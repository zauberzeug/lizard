#include "ble_bridge.h"
#include "../utils/ble_command.h"
#include "../utils/timing.h"
#include "../utils/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>

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

#include <esp_zeug/ble/uuid.h>

// NimBLE runs once per node. The link state only changes on its host task (GAP events, GATT callbacks and the posted
// request); the main task prints the received lines and a BleLineStream task writes the console input.
static constexpr size_t RX_QUEUE_LENGTH = 64;
static constexpr size_t INPUT_QUEUE_LENGTH = 64;
static constexpr size_t QUEUE_BYTES = 8192; // per direction, so that a flood cannot exhaust the heap
static constexpr int MIN_FREE_MBUFS = 12;   // keep buffers for incoming traffic while writing
static constexpr int32_t SCAN_MS = 10000;
static constexpr int32_t CONNECT_TIMEOUT_MS = 5000;
static constexpr uint32_t RECONNECT_MS = 500;
static constexpr uint32_t RETRY_AFTER_FAILURE_MS = 5000;
static constexpr unsigned long LOST_WARNING_INTERVAL_MS = 1000;
static constexpr size_t MAX_PRINT_PER_STEP = 1024; // printing blocks the main loop, which also has to read the host's lines

constexpr ble_uuid128_t uuid128_from_str(const char *str) {
    ble_uuid128_t result{BLE_UUID_TYPE_128, {0}};
    ZZ::Ble::Uuid::parse(std::string_view{str}, result.value, 16);
    return result;
}

static constexpr ble_uuid128_t svc_uuid = uuid128_from_str(CONFIG_ZZ_BLE_COM_SVC_UUID);
static constexpr ble_uuid128_t console_in_uuid = uuid128_from_str(CONFIG_ZZ_BLE_COM_CONSOLE_IN_CHR_UUID);
static constexpr ble_uuid128_t console_out_uuid = uuid128_from_str(CONFIG_ZZ_BLE_COM_CONSOLE_OUT_CHR_UUID);

enum class LinkState {
    IDLE,
    SCANNING,
    CONNECTING,
    CONNECTED,
};

// request of the main task, applied on the host task
static std::mutex request_mutex;
static std::string requested_target;
static uint32_t requested_pin = 0;
static struct ble_npl_event request_event;
static struct ble_npl_callout scan_callout;

// host task only
static LinkState link_state = LinkState::IDLE;
static std::string target;
static uint32_t pin = 0;
static bool synced = false;
static bool backoff = false; // the last attempt failed: wait longer before the next one
static uint8_t own_addr_type = 0;
static uint16_t service_start = 0;
static uint16_t service_end = 0;
static char *rx_line = nullptr; // CONSOLE_LINE_SIZE bytes, allocated with the bridge
static size_t rx_len = 0;
static bool rx_discarding = false;

// shared with the main task
static LineQueue *rx_queue = nullptr;
static std::atomic<uint16_t> conn_handle{BLE_HS_CONN_HANDLE_NONE};
static std::atomic<uint16_t> console_in_handle{0};
static std::atomic<uint16_t> console_out_handle{0};
static std::atomic<bool> linked{false};
static std::atomic<bool> pairing_failed{false};
static std::atomic<bool> no_console{false};
static std::atomic<uint32_t> lost_lines{0};

extern "C" void ble_store_config_init(void);
static int on_gap_event(struct ble_gap_event *event, void *arg);

static void start_scan() {
    if (!synced || target.empty() || link_state != LinkState::IDLE) {
        return;
    }
    struct ble_gap_disc_params params = {};
    params.filter_duplicates = 1; // active scan: the robot sends its name in the scan response
    if (ble_gap_disc(own_addr_type, SCAN_MS, &params, on_gap_event, nullptr) == 0) {
        link_state = LinkState::SCANNING;
    } else {
        ble_npl_callout_reset(&scan_callout, ble_npl_time_ms_to_ticks32(RECONNECT_MS));
    }
}

static void on_scan_callout(struct ble_npl_event *) {
    start_scan();
}

static void stop_link() {
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

static void on_request(struct ble_npl_event *) {
    std::string new_target;
    uint32_t new_pin;
    {
        const std::lock_guard<std::mutex> lock(request_mutex);
        new_target = requested_target;
        new_pin = requested_pin;
    }
    if (new_target == target && new_pin == pin) {
        return; // e.g. the same link() again
    }
    target = new_target;
    pin = new_pin;
    backoff = false;
    ble_npl_callout_stop(&scan_callout);
    stop_link();
    start_scan();
}

static void drop_connection(uint16_t conn) {
    backoff = true;
    ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
}

// splits the robot's console stream into lines and hands them to step()
static void receive(const struct os_mbuf *om) {
    for (const struct os_mbuf *m = om; m != nullptr; m = SLIST_NEXT(m, om_next)) {
        for (uint16_t i = 0; i < m->om_len; i++) {
            const char c = m->om_data[i];
            if (c != '\n') {
                if (rx_len + 1 < CONSOLE_LINE_SIZE) {
                    rx_line[rx_len++] = c;
                } else {
                    rx_discarding = true;
                }
                continue;
            }
            if (rx_discarding || !rx_queue->push_copy(rx_line, rx_len)) {
                lost_lines.fetch_add(1, std::memory_order_relaxed);
            }
            rx_len = 0;
            rx_discarding = false;
        }
    }
}

static int on_subscribed(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *, void *) {
    if (error->status == 0) {
        linked = true;
    } else {
        drop_connection(conn);
    }
    return 0;
}

static int on_characteristic(uint16_t conn, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr, void *) {
    if (error->status == 0) {
        if (ble_uuid_cmp(&chr->uuid.u, &console_in_uuid.u) == 0) {
            console_in_handle = chr->val_handle;
        } else if (ble_uuid_cmp(&chr->uuid.u, &console_out_uuid.u) == 0) {
            console_out_handle = chr->val_handle;
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
        no_console = true;
    }
    drop_connection(conn);
    return 0;
}

static int on_service(uint16_t conn, const struct ble_gatt_error *error, const struct ble_gatt_svc *service, void *) {
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
        no_console = true;
    }
    drop_connection(conn);
    return 0;
}

static int on_mtu(uint16_t, const struct ble_gatt_error *, uint16_t, void *) {
    return 0;
}

static int on_gap_event(struct ble_gap_event *event, void *) {
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        if (link_state != LinkState::SCANNING) {
            return 0;
        }
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0 || fields.name == nullptr ||
            fields.name_len != target.size() || memcmp(fields.name, target.data(), target.size()) != 0) {
            return 0;
        }
        ble_gap_disc_cancel();
        struct ble_gap_conn_params params = {};
        params.scan_itvl = 0x10;
        params.scan_window = 0x10;
        params.itvl_min = 6; // 7.5 ms, the shortest interval: lowest latency and quick retransmissions
        params.itvl_max = 6;
        params.latency = 0;
        params.supervision_timeout = 200; // 2 s
        if (ble_gap_connect(own_addr_type, &event->disc.addr, CONNECT_TIMEOUT_MS, &params, on_gap_event, nullptr) == 0) {
            link_state = LinkState::CONNECTING;
        } else {
            link_state = LinkState::IDLE;
            ble_npl_callout_reset(&scan_callout, ble_npl_time_ms_to_ticks32(RECONNECT_MS));
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
            ble_npl_callout_reset(&scan_callout, ble_npl_time_ms_to_ticks32(RECONNECT_MS));
            return 0;
        }
        link_state = LinkState::CONNECTED;
        conn_handle = event->connect.conn_handle;
        console_in_handle = 0;
        console_out_handle = 0;
        service_start = 0;
        service_end = 0;
        ble_gap_set_data_len(event->connect.conn_handle, 0xFB, 0x0848);
        ble_gattc_exchange_mtu(event->connect.conn_handle, on_mtu, nullptr);
        ble_gap_security_initiate(event->connect.conn_handle); // encrypts with a stored bond or pairs with the PIN
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
            struct ble_sm_io io = {};
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
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }
            pairing_failed = true;
            drop_connection(event->enc_change.conn_handle);
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.attr_handle == console_out_handle) {
            receive(event->notify_rx.om);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        linked = false;
        link_state = LinkState::IDLE;
        rx_len = 0;
        rx_discarding = false;
        ble_npl_callout_reset(&scan_callout, ble_npl_time_ms_to_ticks32(backoff ? RETRY_AFTER_FAILURE_MS : RECONNECT_MS));
        backoff = false;
        return 0;

    default:
        return 0;
    }
}

static void on_sync() {
    if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        return;
    }
    synced = true;
    start_scan();
}

static void on_reset(int) {
    synced = false;
    linked = false;
    conn_handle = BLE_HS_CONN_HANDLE_NONE;
    link_state = LinkState::IDLE;
}

static void run_host_task(void *) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void request(const std::string &new_target, const uint32_t new_pin) {
    {
        const std::lock_guard<std::mutex> lock(request_mutex);
        requested_target = new_target;
        requested_pin = new_pin;
    }
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &request_event);
}

const std::map<std::string, Variable_ptr> BleBridge::get_defaults() {
    return {
        {"link", std::make_shared<StringVariable>("")},
        {"connected", std::make_shared<BooleanVariable>(false)},
        {"rx", std::make_shared<IntegerVariable>(0)},
        {"tx", std::make_shared<IntegerVariable>(0)},
        {"lost", std::make_shared<IntegerVariable>(0)},
    };
}

BleBridge::BleBridge(const std::string name) : ConsoleBridge(name) {
    this->properties = BleBridge::get_defaults();
    if (!ZZ::BleCommand::claim_host(TYPE)) {
        throw std::runtime_error("the BLE radio is already used by the Bluetooth module");
    }
    rx_line = new char[CONSOLE_LINE_SIZE];
    rx_queue = new LineQueue(RX_QUEUE_LENGTH, QUEUE_BYTES);
    for (const char *tag : {"BTDM_INIT", "phy_init"}) {
        esp_log_level_set(tag, ESP_LOG_WARN); // keep the radio's init chatter off the host's console
    }
    const esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        throw std::runtime_error(std::string("nimble_port_init failed: ") + esp_err_to_name(err));
    }
    ble_store_config_init();
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_KEYBOARD_ONLY; // the robot shows its PIN, the bridge enters it
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(name.c_str());
    ble_npl_event_init(&request_event, on_request, nullptr);
    ble_npl_callout_init(&scan_callout, nimble_port_get_dflt_eventq(), on_scan_callout, nullptr);
    nimble_port_freertos_init(run_host_task);
    this->console_input = std::make_unique<BleLineStream>(
        "ble_bridge", INPUT_QUEUE_LENGTH, QUEUE_BYTES, [] { return linked.load(); },
        [] {
            const uint16_t mtu = ble_att_mtu(conn_handle);
            return mtu > 3 ? static_cast<size_t>(mtu - 3) : static_cast<size_t>(20);
        },
        [](const char *data, size_t len) {
            if (os_msys_num_free() < MIN_FREE_MBUFS) {
                return BLE_HS_ENOMEM; // keep buffers for incoming traffic
            }
            return ble_gattc_write_no_rsp_flat(conn_handle, console_in_handle, data, len);
        });
}

void BleBridge::forward(const char *line, size_t len) {
    if (linked && this->console_input->push(line, len)) {
        this->tx++;
    } else {
        lost_lines.fetch_add(1, std::memory_order_relaxed);
    }
}

void BleBridge::step() {
    size_t printed = 0;
    while (printed < MAX_PRINT_PER_STEP) {
        const std::unique_ptr<char[]> line = rx_queue->pop();
        if (!line) {
            break;
        }
        bool checksum_ok = true;
        const int len = check(line.get(), strlen(line.get()), &checksum_ok);
        if (!checksum_ok) {
            this->lost++;
            continue;
        }
        print_remote(line.get()); // echo() adds the checksum again
        printed += len + 1;
        this->rx++;
    }

    if (linked != this->was_linked) {
        this->was_linked = !this->was_linked;
        if (this->was_linked) {
            this->linked_name = this->link_target;
            echo("%s: linked to \"%s\"", this->name.c_str(), this->linked_name.c_str());
        } else {
            echo("%s: link to \"%s\" closed", this->name.c_str(), this->linked_name.c_str());
        }
    }
    if (pairing_failed.exchange(false)) {
        echo("warning: %s could not pair with \"%s\", check the PIN", this->name.c_str(), this->link_target.c_str());
    }
    if (no_console.exchange(false)) {
        echo("warning: \"%s\" offers no Bluetooth console (Lizard too old?)", this->link_target.c_str());
    }
    this->lost += lost_lines.exchange(0, std::memory_order_relaxed);
    if (this->lost > this->lost_reported && millis_since(this->last_lost_warning_ms) >= LOST_WARNING_INTERVAL_MS) {
        echo("warning: %s dropped %lld console lines", this->name.c_str(), (long long)(this->lost - this->lost_reported));
        this->lost_reported = this->lost;
        this->last_lost_warning_ms = millis();
    }

    this->get_property("connected")->set_boolean_value(linked);
    this->get_property("rx")->set_integer_value(this->rx);
    this->get_property("tx")->set_integer_value(this->tx);
    this->get_property("lost")->set_integer_value(this->lost);
    Module::step();
}

void BleBridge::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "link") {
        if (arguments.size() < 1 || arguments.size() > 2) {
            throw std::runtime_error("expecting 1 or 2 arguments (device_name[, pin])");
        }
        Module::expect(arguments, -1, string, integer);
        const std::string device_name = arguments[0]->evaluate_string();
        if (device_name.empty() || device_name.size() > MAX_DEVICE_NAME) {
            throw std::runtime_error("device name must be 1..29 characters");
        }
        const int64_t new_pin = arguments.size() > 1 ? arguments[1]->evaluate_integer() : CONFIG_ZZ_BLE_DEV_PIN;
        if (new_pin < 0 || new_pin > 999999) {
            throw std::runtime_error("PIN must be a 6-digit non-negative integer (000000-999999)");
        }
        this->set_link(device_name);
        request(device_name, static_cast<uint32_t>(new_pin));
    } else if (method_name == "unlink") {
        Module::expect(arguments, 0);
        this->set_link("");
        request("", 0);
    } else {
        Module::call(method_name, arguments);
    }
}

static Module_ptr create_ble_bridge(const std::string &name,
                                    const std::vector<ConstExpression_ptr> &arguments,
                                    MessageHandler) {
    Module::expect(arguments, 0);
    return std::make_shared<BleBridge>(name);
}
REGISTER_MODULE(BleBridge, &create_ble_bridge)
