/*
 * SPDX-FileCopyrightText: 2022 Zauberzeug GmbH
 *
 * SPDX-License-Identifier: MIT
 */

#include "ble_command.h"

#include <array>
#include <cassert>
#include <cstring>

#include <esp_bt.h>
#include <esp_log.h>
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

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

#include "../storage.h"
#include "../utils/uart.h"
#include "sdkconfig.h"

namespace ZZ::BleCommand {

static const char *TAG = "BleCommand";

// Idle timeout - kick clients that don't send app command
static constexpr uint32_t IDLE_TIMEOUT_MS = 15000; // 15 seconds

// State variables
static CommandCallback l_clientCallback{};
static std::array<char, 30> l_deviceName{};
static uint16_t l_currentCon = BLE_HS_CONN_HANDLE_NONE;
static uint16_t l_sendChrValHandle = 0;
static uint8_t l_ownAddrType = 0;
static bool l_running = false;
static bool l_deactivated = false;
static bool l_authenticated = false; // True after encryption established
static bool l_appActive = false;     // True after first command received
static TimerHandle_t l_idleTimer = nullptr;

// UUIDs from Kconfig
static ble_uuid128_t l_svcUuid;
static ble_uuid128_t l_cmdChrUuid;
static ble_uuid128_t l_sendChrUuid;

// 16-bit Alert Notification Service UUID for app filtering
static const ble_uuid16_t l_alertUuid = BLE_UUID16_INIT(0x1811);

// Forward declarations
static void advertise();
static int on_gap_event(struct ble_gap_event *event, void *arg);
static int on_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg);

// External declaration for NimBLE store init
extern "C" void ble_store_config_init(void);

// Idle timer callback - disconnect if no app activity
static void idle_timer_callback(TimerHandle_t timer) {
    if (l_currentCon != BLE_HS_CONN_HANDLE_NONE && !l_appActive) {
        ESP_LOGW(TAG, "Idle timeout - no app activity, disconnecting");
        ble_gap_terminate(l_currentCon, BLE_ERR_REM_USER_CONN_TERM);
    }
}

// Parse UUID string from Kconfig into ble_uuid128_t
static bool parse_uuid128(const char *str, ble_uuid128_t *uuid) {
    if (!str || strlen(str) != 36) {
        return false;
    }

    uint8_t tmp[16];
    int ret = sscanf(str,
                     "%02hhx%02hhx%02hhx%02hhx-"
                     "%02hhx%02hhx-"
                     "%02hhx%02hhx-"
                     "%02hhx%02hhx-"
                     "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",
                     &tmp[15], &tmp[14], &tmp[13], &tmp[12],
                     &tmp[11], &tmp[10],
                     &tmp[9], &tmp[8],
                     &tmp[7], &tmp[6],
                     &tmp[5], &tmp[4], &tmp[3], &tmp[2], &tmp[1], &tmp[0]);

    if (ret != 16) {
        return false;
    }

    uuid->u.type = BLE_UUID_TYPE_128;
    memcpy(uuid->value, tmp, 16);
    return true;
}

// GATT service definition
// Using _ENC flags - NimBLE will automatically require encryption/pairing
static const struct ble_gatt_svc_def l_gattSvcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &l_svcUuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                // Command characteristic - requires encryption to write
                .uuid = &l_cmdChrUuid.u,
                .access_cb = on_chr_access,
                .arg = NULL,
                .descriptors = NULL,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN,
                .min_key_size = 0,
                .val_handle = NULL,
            },
            {
                // Send/Reply characteristic - notify only
                .uuid = &l_sendChrUuid.u,
                .access_cb = on_chr_access,
                .arg = NULL,
                .descriptors = NULL,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .min_key_size = 0,
                .val_handle = &l_sendChrValHandle,
            },
            {0}},
    },
    {0}};

// Start advertising
static void advertise() {
    if (!l_running) {
        return;
    }

    int rc;

    // Primary advertisement: flags + 16-bit UUID for app filtering
    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids16 = &l_alertUuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set advertising fields; rc=%d", rc);
        return;
    }

    // Scan response: device name
    struct ble_hs_adv_fields rsp_fields = {};
    rsp_fields.name = (uint8_t *)l_deviceName.data();
    rsp_fields.name_len = strlen(l_deviceName.data());
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set scan response; rc=%d", rc);
        return;
    }

    // Start advertising
    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(l_ownAddrType, NULL, BLE_HS_FOREVER,
                           &adv_params, on_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising; rc=%d", rc);
    }
}

// GAP event handler
static int on_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);

        if (event->connect.status == 0) {
            l_currentCon = event->connect.conn_handle;

            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
                ESP_LOGI(TAG, "Connected to %02x:%02x:%02x:%02x:%02x:%02x",
                         desc.peer_id_addr.val[5], desc.peer_id_addr.val[4],
                         desc.peer_id_addr.val[3], desc.peer_id_addr.val[2],
                         desc.peer_id_addr.val[1], desc.peer_id_addr.val[0]);
            }
            // DO NOT initiate security here - let the phone do it when
            // it tries to access the encrypted characteristic
        } else {
            advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
        if (event->disconnect.conn.conn_handle == l_currentCon) {
            l_currentCon = BLE_HS_CONN_HANDLE_NONE;
            l_authenticated = false;
            l_appActive = false;
            if (l_idleTimer) {
                xTimerStop(l_idleTimer, 0);
            }
        }
        advertise();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: %d", event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe event: conn=%d attr=%d notify=%d indicate=%d",
                 event->subscribe.conn_handle,
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify,
                 event->subscribe.cur_indicate);

        // When app subscribes to our notify characteristic, send a wake-up notification
        // This may help apps that wait for data before sending their first command
        if (event->subscribe.cur_notify &&
            event->subscribe.attr_handle == l_sendChrValHandle) {
            ESP_LOGI(TAG, "App subscribed to notify - sending wake-up");
            const char *wake = "\n";
            struct os_mbuf *om = ble_hs_mbuf_from_flat(wake, 1);
            if (om) {
                ble_gattc_notify_custom(event->subscribe.conn_handle, l_sendChrValHandle, om);
            }
        }
        return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
        ESP_LOGI(TAG, "Notify TX complete: conn=%d status=%d",
                 event->notify_tx.conn_handle,
                 event->notify_tx.status);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "Encryption change; status=%d", event->enc_change.status);
        if (event->enc_change.status == 0) {
            l_authenticated = true;

            // Always start idle timer - kick if no app command within timeout
            ESP_LOGI(TAG, "Starting idle timeout (%lu ms)", (unsigned long)IDLE_TIMEOUT_MS);
            if (l_idleTimer && !l_appActive) {
                xTimerStart(l_idleTimer, 0);
            }
        } else {
            // Encryption failed - likely key mismatch after reset_bonds
            ESP_LOGW(TAG, "Encryption failed (status=%d) - clearing peer bond and disconnecting",
                     event->enc_change.status);
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }

            // Drop connection immediately so the next attempt starts clean
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        // Phone initiated pairing - display our PIN
        if (l_deactivated) {
            ESP_LOGW(TAG, "Security deactivated - auto-accepting");
            struct ble_sm_io pk = {};
            pk.action = event->passkey.params.action;
            pk.passkey = 0;
            return ble_sm_inject_io(event->passkey.conn_handle, &pk);
        }

        ESP_LOGI(TAG, "Passkey action: %d", event->passkey.params.action);

        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            // Get PIN
            uint32_t user_pin = 0;
            uint32_t dev_pin = CONFIG_ZZ_BLE_DEV_PIN;
            bool has_user_pin = Storage::get_user_pin(user_pin);
            uint32_t pin = has_user_pin ? user_pin : dev_pin;

            ESP_LOGI(TAG, "Displaying PIN: %06lu", (unsigned long)pin);
            echo("BLE PIN: %06lu", (unsigned long)pin);

            struct ble_sm_io pk = {};
            pk.action = BLE_SM_IOACT_DISP;
            pk.passkey = pin;
            return ble_sm_inject_io(event->passkey.conn_handle, &pk);

        } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            ESP_LOGI(TAG, "Numeric comparison: %06lu", (unsigned long)event->passkey.params.numcmp);
            struct ble_sm_io pk = {};
            pk.action = BLE_SM_IOACT_NUMCMP;
            pk.numcmp_accept = 1;
            return ble_sm_inject_io(event->passkey.conn_handle, &pk);
        }
        return 0;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // Phone wants to re-pair - delete old bond and allow
        ESP_LOGI(TAG, "Repeat pairing requested");
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    default:
        return 0;
    }
}

// GATT characteristic access callback
// Security is enforced by NimBLE via _ENC flags - no manual check needed
static int on_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg) {
    // Command characteristic (write)
    if (ble_uuid_cmp(ctxt->chr->uuid, &l_cmdChrUuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            // Mark app as active - stop idle timer
            if (!l_appActive) {
                l_appActive = true;
                if (l_idleTimer) {
                    xTimerStop(l_idleTimer, 0);
                }
                ESP_LOGI(TAG, "App activity detected - connection kept alive");
            }

            uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
            if (om_len > 0 && l_clientCallback) {
                char *buf = (char *)malloc(om_len + 1);
                if (buf) {
                    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, om_len, NULL);
                    if (rc == 0) {
                        buf[om_len] = '\0';
                        ESP_LOGI(TAG, "Command: %.*s", (int)om_len, buf);
                        l_clientCallback(std::string_view(buf, om_len));
                    }
                    free(buf);
                }
            }
            return 0;
        }
    }

    // Send characteristic (notify only, no read)
    if (ble_uuid_cmp(ctxt->chr->uuid, &l_sendChrUuid.u) == 0) {
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

// NimBLE host task
static void host_task(void *param) {
    ESP_LOGI(TAG, "BLE Host task started");
    nimble_port_run();
    nimble_port_deinit();
    ESP_LOGI(TAG, "BLE Host task stopped");
}

// Sync callback - called when host syncs with controller
static void on_sync() {
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to ensure address; rc=%d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &l_ownAddrType);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to infer address type; rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG, "BLE synced, address type=%d", l_ownAddrType);
    advertise();
}

static void on_reset(int reason) {
    ESP_LOGW(TAG, "BLE Host reset; reason=%d", reason);
}

// Public API

auto init(const std::string_view &deviceName, CommandCallback onCommand) -> void {
    ESP_LOGI(TAG, "Initializing BLE with device name: %.*s",
             (int)deviceName.length(), deviceName.data());

    if (l_running) {
        ESP_LOGW(TAG, "BLE already running");
        return;
    }

    l_clientCallback = onCommand;
    size_t name_len = deviceName.length() < l_deviceName.size() - 1
                          ? deviceName.length()
                          : l_deviceName.size() - 1;
    memcpy(l_deviceName.data(), deviceName.data(), name_len);
    l_deviceName[name_len] = '\0';

    // Parse UUIDs
    if (!parse_uuid128(CONFIG_ZZ_BLE_COM_SVC_UUID, &l_svcUuid) ||
        !parse_uuid128(CONFIG_ZZ_BLE_COM_CHR_UUID, &l_cmdChrUuid) ||
        !parse_uuid128(CONFIG_ZZ_BLE_COM_SEND_CHR_UUID, &l_sendChrUuid)) {
        ESP_LOGE(TAG, "Failed to parse UUIDs");
        return;
    }

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Initialize NimBLE
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Initialize bond storage
    ble_store_config_init();

    // Configure host
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;

    // Configure security manager - BEFORE starting host
    // Display-only device: we show PIN, phone enters it
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    // Initialize services
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Register GATT services
    int rc = ble_gatts_count_cfg(l_gattSvcs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(l_gattSvcs);
    assert(rc == 0);

    ble_svc_gap_device_name_set(l_deviceName.data());

    // Create idle timer
    if (!l_idleTimer) {
        l_idleTimer = xTimerCreate("ble_idle", pdMS_TO_TICKS(IDLE_TIMEOUT_MS),
                                   pdFALSE, nullptr, idle_timer_callback);
    }

    // Start host task
    nimble_port_freertos_init(host_task);

    l_running = true;
    ESP_LOGI(TAG, "BLE initialized (idle timeout: %lums)", (unsigned long)IDLE_TIMEOUT_MS);
}

auto send(const std::string_view &data) -> int {
    if (l_currentCon == BLE_HS_CONN_HANDLE_NONE) {
        return BLE_HS_ENOTCONN;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data.data(), data.length());
    if (!om) {
        return BLE_HS_ENOMEM;
    }

    return ble_gattc_notify_custom(l_currentCon, l_sendChrValHandle, om);
}

auto fini() -> void {
    if (!l_running) {
        return;
    }
    if (nimble_port_stop() == 0) {
        nimble_port_deinit();
    }
    l_running = false;
}

auto deactivate_pin() -> void {
    l_deactivated = true;
    ESP_LOGW(TAG, "Bluetooth security deactivated");
}

auto reset_bonds() -> void {
    ESP_LOGI(TAG, "Resetting all bonds...");

    // Terminate current connection if any
    if (l_currentCon != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(l_currentCon, BLE_ERR_REM_USER_CONN_TERM);
        l_currentCon = BLE_HS_CONN_HANDLE_NONE;
    }

    // Clear NimBLE's bond store using the proper API
    // This clears both the in-memory cache AND the NVS storage
    int rc = ble_store_clear();
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_store_clear failed: %d, trying NVS fallback", rc);

        // Fallback: clear NVS directly
        auto eraseNamespace = [](const char *ns) {
            nvs_handle_t h{};
            if (nvs_open(ns, NVS_READWRITE, &h) == ESP_OK) {
                nvs_erase_all(h);
                nvs_commit(h);
                nvs_close(h);
            }
        };
        eraseNamespace("nimble_bond");
        eraseNamespace("nimble_cccd");
    }

    // Stop and restart advertising to refresh state
    ble_gap_adv_stop();
    advertise();

    ESP_LOGI(TAG, "Bonds cleared - all peers must re-pair");
}

} // namespace ZZ::BleCommand
