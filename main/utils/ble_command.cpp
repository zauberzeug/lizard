/*
 * SPDX-FileCopyrightText: 2022 Zauberzeug GmbH
 *
 * SPDX-License-Identifier: MIT
 */

#include "ble_command.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <new>

#include <nvs.h>
#include <nvs_flash.h>

#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_store.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
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

#include "../storage.h"
#include "../utils/uart.h"
#include "sdkconfig.h"

#ifndef ZZ_BLE_DEBUG
#define ZZ_BLE_DEBUG 0
#endif

namespace ZZ::BleCommand {

static constexpr uint32_t IDLE_TIMEOUT_MS = 30000; // Kick clients that don't authenticate (leaves time to type the PIN)
static constexpr uint8_t MAX_PIN_FAILURES = 3;     // Wrong PINs per connection before it is closed
static constexpr size_t MAX_DEVICE_NAME_LEN = 30;
static constexpr uint16_t TX_DATA_LENGTH = 0xFB;
static constexpr uint16_t TX_DATA_TIME = 0x0848;

constexpr ble_uuid128_t uuid128_from_str(const char *str) {
    ble_uuid128_t result{BLE_UUID_TYPE_128, {0}};
    ZZ::Ble::Uuid::parse(std::string_view{str}, result.value, 16);
    return result;
}

static constexpr ble_uuid128_t svc_uuid = uuid128_from_str(CONFIG_ZZ_BLE_COM_SVC_UUID);
static constexpr ble_uuid128_t cmd_chr_uuid = uuid128_from_str(CONFIG_ZZ_BLE_COM_CHR_UUID);
static constexpr ble_uuid128_t send_chr_uuid = uuid128_from_str(CONFIG_ZZ_BLE_COM_SEND_CHR_UUID);

static CommandCallback client_callback{};
static std::array<char, MAX_DEVICE_NAME_LEN> ble_device_name{};
static std::atomic<uint16_t> current_con{BLE_HS_CONN_HANDLE_NONE};
static uint16_t send_chr_val_handle = 0;
static uint8_t own_addr_type = 0;
static bool running = false;
static bool pin_deactivated = false;
static std::atomic<bool> authenticated{false};
static uint8_t pin_failures = 0;
static ble_npl_callout idle_timer; // posts to the NimBLE host task, so it runs in line with the GAP and GATT callbacks

// 16-bit Alert Notification Service UUID for app filtering
static const ble_uuid16_t alert_uuid = BLE_UUID16_INIT(0x1811);

static void advertise();
static int on_gap_event(struct ble_gap_event *event, void *arg);
static int on_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg);

extern "C" void ble_store_config_init(void);

static void send_notification(uint16_t conn_handle, const char *msg) {
    struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, strlen(msg));
    if (om)
        ble_gattc_notify_custom(conn_handle, send_chr_val_handle, om);
}

// The user PIN from NVS or, without one, the PIN configured at build time
static uint32_t expected_pin() {
    uint32_t user_pin = 0;
    return Storage::get_user_pin(user_pin) ? user_pin : CONFIG_ZZ_BLE_DEV_PIN;
}

static bool check_pin(const char *buf, uint16_t len) {
    // Expect "AUTH <pin>" where pin is up to 6 digits
    if (len < 5 || strncmp(buf, "AUTH ", 5) != 0) {
        return false;
    }

    const char *pin_str = buf + 5;
    uint16_t pin_len = len - 5;
    // Ignore a trailing line end or spaces, e.g. "AUTH 123456\r\n"
    while (pin_len > 0 && (pin_str[pin_len - 1] == '\n' || pin_str[pin_len - 1] == '\r' || pin_str[pin_len - 1] == ' ')) {
        pin_len--;
    }
    if (pin_len == 0 || pin_len > 6) {
        return false;
    }

    // Parse PIN from message
    uint32_t received_pin = 0;
    for (uint16_t i = 0; i < pin_len; i++) {
        if (pin_str[i] < '0' || pin_str[i] > '9') {
            return false;
        }
        received_pin = received_pin * 10 + (pin_str[i] - '0');
    }

    return received_pin == expected_pin();
}

static void start_idle_timer() {
    ble_npl_callout_reset(&idle_timer, ble_npl_time_ms_to_ticks32(IDLE_TIMEOUT_MS));
}

static void stop_idle_timer() {
    ble_npl_callout_stop(&idle_timer);
}

static void on_idle_timeout(ble_npl_event * /* event */) {
    if (current_con != BLE_HS_CONN_HANDLE_NONE && !authenticated) {
        echo("BLE: kicking unauthenticated client (timeout)");
        ble_gap_terminate(current_con, BLE_ERR_REM_USER_CONN_TERM);
    }
}

// No BLE-level security - authentication is done at application level via AUTH command
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .includes = NULL,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                // Command characteristic - app-level auth via AUTH command
                .uuid = &cmd_chr_uuid.u,
                .access_cb = on_chr_access,
                .arg = NULL,
                .descriptors = NULL,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .min_key_size = 0,
                .val_handle = NULL,
                .cpfd = NULL,
            },
            {
                // Send/Reply characteristic - notify only
                .uuid = &send_chr_uuid.u,
                .access_cb = on_chr_access,
                .arg = NULL,
                .descriptors = NULL,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .min_key_size = 0,
                .val_handle = &send_chr_val_handle,
                .cpfd = NULL,
            },
            {}},
    },
    {}};

static void advertise() {
    if (!running) {
        return;
    }

    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids16 = &alert_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    if (ble_gap_adv_set_fields(&fields) != 0) {
        return;
    }

    struct ble_hs_adv_fields rsp_fields = {};
    rsp_fields.name = (uint8_t *)ble_device_name.data();
    rsp_fields.name_len = strlen(ble_device_name.data());
    rsp_fields.name_is_complete = 1;

    if (ble_gap_adv_rsp_set_fields(&rsp_fields) != 0) {
        return;
    }

    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, on_gap_event, NULL);
}

static int on_gap_event(struct ble_gap_event *event, void * /* arg */) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            current_con = event->connect.conn_handle;
            pin_failures = 0;

            ble_gap_set_data_len(event->connect.conn_handle, TX_DATA_LENGTH, TX_DATA_TIME);

            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
                const uint8_t *addr = desc.peer_id_addr.val;
                echo("BLE: connected %02x:%02x:%02x:%02x:%02x:%02x",
                     addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
            }

            if (!pin_deactivated) {
                echo("BLE PIN: %06lu", (unsigned long)expected_pin());
            }

            start_idle_timer();
        } else {
            advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT: {
        const uint8_t *addr = event->disconnect.conn.peer_id_addr.val;
        int reason = event->disconnect.reason;
        echo("BLE: disconnected %02x:%02x:%02x:%02x:%02x:%02x (reason=%d)",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], reason);

        if (event->disconnect.conn.conn_handle == current_con) {
            current_con = BLE_HS_CONN_HANDLE_NONE;
            authenticated = false;
            stop_idle_timer();
        }
        advertise();
        return 0;
    }

    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        // Signal readiness on subscribe; AUTH_REQUIRED tells the app to ask for the PIN
        if (event->subscribe.cur_notify) {
            send_notification(event->subscribe.conn_handle, authenticated || pin_deactivated ? "\n" : "AUTH_REQUIRED\n");
        }
        return 0;

    default:
        return 0;
    }
}

static int on_chr_access(uint16_t conn_handle, uint16_t /* attr_handle */,
                         struct ble_gatt_access_ctxt *ctxt, void * /* arg */) {
    if (ble_uuid_cmp(ctxt->chr->uuid, &cmd_chr_uuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            if (len == 0) {
                return 0;
            }

            std::unique_ptr<char[]> buf(new (std::nothrow) char[len + 1]);
            if (!buf || ble_hs_mbuf_to_flat(ctxt->om, buf.get(), len, NULL) != 0) {
                return BLE_ATT_ERR_UNLIKELY;
            }
            buf[len] = '\0';

#if ZZ_BLE_DEBUG
            echo("BLE rx: %s", buf.get());
#endif

            // Handle AUTH until the client is authenticated; afterwards it is an ordinary line
            if (!authenticated && len >= 5 && strncmp(buf.get(), "AUTH ", 5) == 0) {
                if (pin_failures >= MAX_PIN_FAILURES) {
                    return 0; // the connection is already closing, so queued guesses are not checked anymore
                }
                if (pin_deactivated || check_pin(buf.get(), len)) {
                    authenticated = true;
                    stop_idle_timer();
                    echo("BLE: authenticated%s", pin_deactivated ? " (PIN deactivated)" : " via app PIN");
                    send_notification(conn_handle, "POST /notification Connected\n");
                } else {
                    echo("BLE: wrong PIN");
                    send_notification(conn_handle, "AUTH_FAIL\n");
                    if (++pin_failures >= MAX_PIN_FAILURES) {
                        echo("BLE: too many wrong PINs, disconnecting");
                        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                    } else {
                        start_idle_timer();
                    }
                }
                return 0;
            }

            // Reject non-AUTH commands before authentication (unless PIN is deactivated)
            if (!authenticated && !pin_deactivated) {
                echo("BLE: rejected command before authentication");
                send_notification(conn_handle, "AUTH_REQUIRED\n");
                return 0;
            }

            // Mark as authenticated on first command when PIN is deactivated
            if (!authenticated && pin_deactivated) {
                authenticated = true;
                stop_idle_timer();
            }

            // Process command normally
            if (client_callback) {
                client_callback(std::move(buf));
            }
            return 0;
        }
    }

    if (ble_uuid_cmp(ctxt->chr->uuid, &send_chr_uuid.u) == 0) {
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static void run_host_task(void * /* param */) {
    nimble_port_run();
    nimble_port_deinit();
}

static void on_sync() {
    if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        echo("BLE: failed to initialize address");
        return;
    }
    advertise();
}

static void on_reset(int reason) {
    echo("BLE: host reset (reason=%d)", reason);
}

void init(const std::string_view &device_name, CommandCallback on_command) {
    if (running) {
        return;
    }

    client_callback = on_command;
    const size_t name_len = std::min(device_name.length(), MAX_DEVICE_NAME_LEN - 1);
    memcpy(ble_device_name.data(), device_name.data(), name_len);
    ble_device_name[name_len] = '\0';

    if (esp_err_t ret = nvs_flash_init(); ret != ESP_OK) {
        echo("BLE: nvs_flash_init issue (%s) - NVS should be initialized in main", esp_err_to_name(ret));
    }

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        echo("BLE: nimble_port_init failed (%s)", esp_err_to_name(ret));
        return;
    }

    ble_store_config_init();

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;

    // No BLE-level security - app-level PIN authentication instead
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(gatt_svcs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(gatt_svcs);
    assert(rc == 0);

    ble_svc_gap_device_name_set(ble_device_name.data());

    ble_npl_callout_init(&idle_timer, nimble_port_get_dflt_eventq(), on_idle_timeout, nullptr);

    running = true;
    nimble_port_freertos_init(run_host_task);
}

int send(const std::string_view &data) {
    if (current_con == BLE_HS_CONN_HANDLE_NONE) {
        return BLE_HS_ENOTCONN;
    }
    // Nothing goes out to a client that has not entered the PIN yet
    if (!authenticated && !pin_deactivated) {
        return BLE_HS_EAUTHEN;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data.data(), data.length());
    if (!om) {
        return BLE_HS_ENOMEM;
    }

#if ZZ_BLE_DEBUG
    echo("BLE tx: %.*s", (int)data.length(), data.data());
#endif
    return ble_gattc_notify_custom(current_con, send_chr_val_handle, om);
}

bool is_connected() {
    return current_con != BLE_HS_CONN_HANDLE_NONE;
}

void finalize() {
    if (!running) {
        return;
    }
    stop_idle_timer();
    if (nimble_port_stop() == 0) {
        ble_npl_callout_deinit(&idle_timer);
        nimble_port_deinit();
    }
    running = false;
}

void deactivate_pin() {
    pin_deactivated = true;
}

} // namespace ZZ::BleCommand
