/*
 * SPDX-FileCopyrightText: 2022 Zauberzeug GmbH
 *
 * SPDX-License-Identifier: MIT
 */

#include "ble_command.h"

#include <array>
#include <cassert>
#include <cstring>

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
#include <freertos/timers.h>

#include "../storage.h"
#include "../utils/uart.h"
#include "sdkconfig.h"

namespace ZZ::BleCommand {

static const char *TAG = "BleCommand";

// Idle timeout - kick clients that don't send app command
static constexpr uint32_t IDLE_TIMEOUT_MS = 15000; // 15 seconds
static constexpr size_t MAX_DEVICE_NAME_LEN = 30;
static constexpr size_t UUID_STRING_LENGTH = 36; // Standard UUID format: 8-4-4-4-12 hex digits
static constexpr size_t UUID_BYTE_COUNT = 16;    // 128-bit UUID = 16 bytes

// State variables
static CommandCallback l_clientCallback{};
static std::array<char, MAX_DEVICE_NAME_LEN> l_deviceName{};
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
static int handle_gap_event(struct ble_gap_event *event, void *arg);
static int handle_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg);

// External declaration for NimBLE store init
extern "C" void ble_store_config_init(void);

// FreeRTOS timer callback that terminates connections when no app command is received within timeout
static void handle_idle_timeout(TimerHandle_t /* timer */) {
    if (l_currentCon != BLE_HS_CONN_HANDLE_NONE && !l_appActive) {
        ESP_LOGW(TAG, "Idle timeout - no app activity, disconnecting");
        ble_gap_terminate(l_currentCon, BLE_ERR_REM_USER_CONN_TERM);
    }
}

// NimBLE store status callback - handles bond storage overflow by evicting oldest bond
static int handle_store_status(struct ble_store_status_event *event, void * /* arg */) {
    if (event->event_code == BLE_STORE_EVENT_FULL) {
        ESP_LOGW(TAG, "Bond storage full - evicting oldest bond");

        // Get list of bonded peers
        ble_addr_t bonded_peers[CONFIG_BT_NIMBLE_MAX_BONDS];
        int num_peers = 0;
        int rc = ble_store_util_bonded_peers(bonded_peers, &num_peers, CONFIG_BT_NIMBLE_MAX_BONDS);

        if (rc == 0 && num_peers > 0) {
            // Delete the first (oldest) bond to make room
            ESP_LOGI(TAG, "Removing bond for %02x:%02x:%02x:%02x:%02x:%02x",
                     bonded_peers[0].val[5], bonded_peers[0].val[4],
                     bonded_peers[0].val[3], bonded_peers[0].val[2],
                     bonded_peers[0].val[1], bonded_peers[0].val[0]);
            ble_gap_unpair(&bonded_peers[0]);
            return 0; // Tell NimBLE to retry storing
        }
    }
    return BLE_HS_EUNKNOWN;
}

static bool parse_uuid128(const char *str, ble_uuid128_t *uuid) {
    if (!str || strlen(str) != UUID_STRING_LENGTH) {
        return false;
    }

    uint8_t tmp[UUID_BYTE_COUNT];
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

    if (ret != UUID_BYTE_COUNT) {
        return false;
    }

    uuid->u.type = BLE_UUID_TYPE_128;
    memcpy(uuid->value, tmp, UUID_BYTE_COUNT);
    return true;
}

// Using _ENC flags - NimBLE will automatically require encryption/pairing
static const struct ble_gatt_svc_def l_gattSvcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &l_svcUuid.u,
        .includes = NULL,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                // Command characteristic - requires encryption to write
                .uuid = &l_cmdChrUuid.u,
                .access_cb = handle_chr_access,
                .arg = NULL,
                .descriptors = NULL,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN,
                .min_key_size = 0,
                .val_handle = NULL,
                .cpfd = NULL,
            },
            {
                // Send/Reply characteristic - notify only
                .uuid = &l_sendChrUuid.u,
                .access_cb = handle_chr_access,
                .arg = NULL,
                .descriptors = NULL,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .min_key_size = 0,
                .val_handle = &l_sendChrValHandle,
                .cpfd = NULL,
            },
            {}},
    },
    {}};

static void advertise() {
    if (!l_running) {
        return;
    }

    int rc;

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

    struct ble_hs_adv_fields rsp_fields = {};
    rsp_fields.name = (uint8_t *)l_deviceName.data();
    rsp_fields.name_len = strlen(l_deviceName.data());
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set scan response; rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(l_ownAddrType, NULL, BLE_HS_FOREVER,
                           &adv_params, handle_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising; rc=%d", rc);
    }
}

static int handle_gap_event(struct ble_gap_event *event, void * /* arg */) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);

        if (event->connect.status == 0) {
            l_currentCon = event->connect.conn_handle;

            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
                const uint8_t *addr = desc.peer_id_addr.val;
                ESP_LOGI(TAG, "Connected to %02x:%02x:%02x:%02x:%02x:%02x (bonded=%d)",
                         addr[5], addr[4], addr[3], addr[2], addr[1], addr[0],
                         desc.sec_state.bonded);

                // For bonded devices: initiate encryption (required by iOS reconnection)
                // For new devices: let phone initiate pairing when accessing encrypted characteristic
                if (desc.sec_state.bonded) {
                    ESP_LOGI(TAG, "Bonded peer - initiating encryption");
                    int rc = ble_gap_security_initiate(event->connect.conn_handle);
                    if (rc != 0) {
                        ESP_LOGW(TAG, "Failed to initiate security; rc=%d", rc);
                    }
                }
            }
        } else {
            advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);

        // Detect bond mismatch: disconnect before encryption with reason 531 (0x213)
        // means phone has old keys but ESP32 doesn't (e.g. after reset_bonds)
        if (!l_authenticated && event->disconnect.reason == 0x213) {
            ESP_LOGW(TAG, "Bond mismatch detected - phone needs to forget device in Bluetooth settings");
            echo("BLE: Bond mismatch - phone must forget device in Bluetooth settings");
        }

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

        // Send wake-up notification when app subscribes to help apps that wait for data
        if (event->subscribe.cur_notify &&
            event->subscribe.attr_handle == l_sendChrValHandle) {
            ESP_LOGI(TAG, "App subscribed to notify - sending wake-up");
            constexpr const char *wake = "\n";
            constexpr size_t wake_len = 1;
            struct os_mbuf *om = ble_hs_mbuf_from_flat(wake, wake_len);
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

            // Enforce bond limit - remove oldest bonds if over limit
            constexpr int max_bonds = CONFIG_BT_NIMBLE_MAX_BONDS;
            ble_addr_t bonded_peers[max_bonds + 1];
            int num_peers = 0;
            if (ble_store_util_bonded_peers(bonded_peers, &num_peers, max_bonds + 1) == 0) {
                ESP_LOGI(TAG, "Current bonds: %d (max: %d)", num_peers, max_bonds);
                while (num_peers > max_bonds) {
                    ESP_LOGW(TAG, "Bond limit exceeded - removing oldest bond %02x:%02x:%02x:%02x:%02x:%02x",
                             bonded_peers[0].val[5], bonded_peers[0].val[4],
                             bonded_peers[0].val[3], bonded_peers[0].val[2],
                             bonded_peers[0].val[1], bonded_peers[0].val[0]);
                    ble_gap_unpair(&bonded_peers[0]);
                    // Re-fetch after removal
                    ble_store_util_bonded_peers(bonded_peers, &num_peers, max_bonds + 1);
                }
            }

            ESP_LOGI(TAG, "Starting idle timeout (%lu ms)", (unsigned long)IDLE_TIMEOUT_MS);
            if (l_idleTimer && !l_appActive) {
                xTimerStart(l_idleTimer, 0);
            }
        } else {
            struct ble_gap_conn_desc desc;
            bool was_bonded = false;
            if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
                was_bonded = desc.sec_state.bonded;
            }

            if (was_bonded) {
                // Bonded device with key mismatch - likely after reset_bonds on one side
                // Send error notification before disconnecting
                ESP_LOGW(TAG, "Encryption failed (status=%d) - bond key mismatch, notifying and disconnecting",
                         event->enc_change.status);

                constexpr const char *bond_err = "!bond_error\n";
                struct os_mbuf *om = ble_hs_mbuf_from_flat(bond_err, strlen(bond_err));
                if (om) {
                    ble_gattc_notify_custom(event->enc_change.conn_handle, l_sendChrValHandle, om);
                }
                ble_store_util_delete_peer(&desc.peer_id_addr);
            } else {
                // Fresh pairing failed (e.g., DHKey check error 1035)
                // Just disconnect cleanly - next attempt should work
                ESP_LOGW(TAG, "Fresh pairing failed (status=%d) - disconnecting, please retry",
                         event->enc_change.status);
            }

            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        if (l_deactivated) {
            ESP_LOGW(TAG, "Security deactivated - auto-accepting");
            struct ble_sm_io pk = {};
            pk.action = event->passkey.params.action;
            pk.passkey = 0;
            return ble_sm_inject_io(event->passkey.conn_handle, &pk);
        }

        ESP_LOGI(TAG, "Passkey action: %d", event->passkey.params.action);

        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            uint32_t user_pin = 0;
            const uint32_t dev_pin = CONFIG_ZZ_BLE_DEV_PIN;
            const bool has_user_pin = Storage::get_user_pin(user_pin);
            const uint32_t pin = has_user_pin ? user_pin : dev_pin;

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

// Security is enforced by NimBLE via _ENC flags - no manual check needed
static int handle_chr_access(uint16_t /* conn_handle */, uint16_t /* attr_handle */,
                             struct ble_gatt_access_ctxt *ctxt, void * /* arg */) {
    if (ble_uuid_cmp(ctxt->chr->uuid, &l_cmdChrUuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            if (!l_appActive) {
                l_appActive = true;
                if (l_idleTimer) {
                    xTimerStop(l_idleTimer, 0);
                }
                ESP_LOGI(TAG, "App activity detected - connection kept alive");
            }

            const uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
            if (om_len > 0 && l_clientCallback) {
                constexpr size_t null_terminator_size = 1;
                char *buf = (char *)malloc(om_len + null_terminator_size);
                if (buf) {
                    const int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, om_len, NULL);
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

    if (ble_uuid_cmp(ctxt->chr->uuid, &l_sendChrUuid.u) == 0) {
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static void run_host_task(void * /* param */) {
    ESP_LOGI(TAG, "BLE Host task started");
    nimble_port_run();
    nimble_port_deinit();
    ESP_LOGI(TAG, "BLE Host task stopped");
}

// NimBLE callback that initializes address and starts advertising after BLE stack is ready
static void handle_sync() {
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

static void handle_reset(int reason) {
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
    const size_t name_len = deviceName.length() < (MAX_DEVICE_NAME_LEN - 1)
                                ? deviceName.length()
                                : (MAX_DEVICE_NAME_LEN - 1);
    memcpy(l_deviceName.data(), deviceName.data(), name_len);
    l_deviceName[name_len] = '\0';

    if (!parse_uuid128(CONFIG_ZZ_BLE_COM_SVC_UUID, &l_svcUuid) ||
        !parse_uuid128(CONFIG_ZZ_BLE_COM_CHR_UUID, &l_cmdChrUuid) ||
        !parse_uuid128(CONFIG_ZZ_BLE_COM_SEND_CHR_UUID, &l_sendChrUuid)) {
        ESP_LOGE(TAG, "Failed to parse UUIDs");
        return;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return;
    }

    ble_store_config_init();

    ble_hs_cfg.reset_cb = handle_reset;
    ble_hs_cfg.sync_cb = handle_sync;
    ble_hs_cfg.store_status_cb = handle_store_status;

    // Display-only device: we show PIN, phone enters it
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(l_gattSvcs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(l_gattSvcs);
    assert(rc == 0);

    ble_svc_gap_device_name_set(l_deviceName.data());

    if (!l_idleTimer) {
        l_idleTimer = xTimerCreate("ble_idle", pdMS_TO_TICKS(IDLE_TIMEOUT_MS),
                                   pdFALSE, nullptr, handle_idle_timeout);
    }

    nimble_port_freertos_init(run_host_task);

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

auto finalize() -> void {
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

    // Disconnect any active connection first
    if (l_currentCon != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(l_currentCon, BLE_ERR_REM_USER_CONN_TERM);
        l_currentCon = BLE_HS_CONN_HANDLE_NONE;
    }

    // Stop advertising during reset
    ble_gap_adv_stop();

    // Get all bonded peers and unpair them (clears in-memory security state)
    ble_addr_t bonded_peers[CONFIG_BT_NIMBLE_MAX_BONDS];
    int num_peers = 0;
    int rc = ble_store_util_bonded_peers(bonded_peers, &num_peers, CONFIG_BT_NIMBLE_MAX_BONDS);
    if (rc == 0 && num_peers > 0) {
        ESP_LOGI(TAG, "Unpairing %d bonded peer(s)", num_peers);
        for (int i = 0; i < num_peers; i++) {
            ble_gap_unpair(&bonded_peers[i]);
        }
    }

    // Clear persistent storage
    rc = ble_store_clear();
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_store_clear failed: %d, trying NVS fallback", rc);

        // Lambda that erases all keys in a given NVS namespace to clear bond storage
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

    // Small delay to let NimBLE fully process the unpair/clear
    vTaskDelay(pdMS_TO_TICKS(100));

    advertise();

    ESP_LOGI(TAG, "Bonds cleared - all peers must re-pair");
}

} // namespace ZZ::BleCommand
