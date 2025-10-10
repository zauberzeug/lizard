/*
 * SPDX-FileCopyrightText: 2022 Zauberzeug GmbH
 *
 * SPDX-License-Identifier: MIT
 */

#include "ble_command.h"

#include <cassert>

#include <esp_bt.h>
#include <esp_log.h>
#include <esp_nimble_hci.h>
#include <nvs.h>
#include <nvs_flash.h>
#ifdef min
#undef min // esp-idf/components/bt/host/nimble/nimble/porting/nimble/include/os/os.h:38:19
#endif

#include <host/ble_hs.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

#include <esp_system.h>
#include <esp_zeug/ble/gatts.h>
#include <esp_zeug/ble/uuid.h>
#include <esp_zeug/frtos-util.h>
#include <esp_zeug/util.h>
#include <host/ble_store.h>

#include "sdkconfig.h"

#include "../storage.h"

namespace ZZ::BleCommand {

constexpr ble_uuid128_t uuid128_from_str(const char *str) {
    ble_uuid128_t result{BLE_UUID_TYPE_128, {0}};
    ZZ::Ble::Uuid::parse(std::string_view{str}, result.value, 16);
    return result;
}

constexpr ble_uuid16_t uuid16_from_str(const char *str) {
    ble_uuid16_t result{BLE_UUID_TYPE_16, 0};
    std::uint8_t buf[2]{};
    ZZ::Ble::Uuid::parse(std::string_view{str}, buf, 2);
    result.value = static_cast<uint16_t>(buf[1] << 8 | buf[0]);
    return result;
}

/* This buffer will be used for advertising, so keep the device name
 * truncated to 29 bytes here */
static Util::TextBuffer<29 + 1> l_deviceName{};
static constexpr ble_uuid128_t serviceUuid = uuid128_from_str(CONFIG_ZZ_BLE_COM_SVC_UUID);
static constexpr ble_uuid128_t characteristicUuid = uuid128_from_str(CONFIG_ZZ_BLE_COM_CHR_UUID);
static constexpr ble_uuid128_t notifyCharaUuid = uuid128_from_str(CONFIG_ZZ_BLE_COM_SEND_CHR_UUID);
static constexpr esp_power_level_t defaultPowerLevel{ESP_PWR_LVL_P9};

static bool getDevPin(std::uint32_t &pin) {
#ifdef CONFIG_ZZ_BLE_DEV_PIN
    pin = static_cast<std::uint32_t>(CONFIG_ZZ_BLE_DEV_PIN);
    return true;
#else
    (void)pin;
    return false;
#endif
}

static bool getUserPin(std::uint32_t &pin) { return Storage::get_user_pin(pin); }

/* Range: 0x001B-0x00FB */
static constexpr std::uint16_t txDataLength{0xFB};

/* Range: 0x0148-0x0848 (stated max 0x4290 leads to BLE_HS_ECONTROLLER) */
static constexpr std::uint16_t txDataTime{0x0848};

static const char TAG[]{"BleCom"};

using namespace FrtosUtil;

#ifndef ZZ_BLE_DEBUG
#define ZZ_BLE_DEBUG 0
#endif

#if ZZ_BLE_DEBUG
#define BLE_LOGV(TAG_, FMT_, ...) ESP_LOGV(TAG_, FMT_, ##__VA_ARGS__)
#define BLE_LOGD(TAG_, FMT_, ...) ESP_LOGD(TAG_, FMT_, ##__VA_ARGS__)
#define BLE_LOGI(TAG_, FMT_, ...) ESP_LOGI(TAG_, FMT_, ##__VA_ARGS__)
#define BLE_LOGW(TAG_, FMT_, ...) ESP_LOGW(TAG_, FMT_, ##__VA_ARGS__)
#else
#define BLE_LOGV(TAG_, FMT_, ...) \
    do {                          \
    } while (0)
#define BLE_LOGD(TAG_, FMT_, ...) \
    do {                          \
    } while (0)
#define BLE_LOGI(TAG_, FMT_, ...) \
    do {                          \
    } while (0)
#define BLE_LOGW(TAG_, FMT_, ...) \
    do {                          \
    } while (0)
#endif

static uint8_t l_ownAddrType;
static CommandCallback l_clientCallback;
static bool l_running{false};
static bool l_deactivated{false};

static std::uint16_t l_notifyCharaValueHandle;
static std::uint16_t l_currentCon{BLE_HS_CONN_HANDLE_NONE};

static auto advertise() -> void;
static auto onSecurityEvent(struct ble_gap_event *event, void *) -> int;

static auto onGapEvent(struct ble_gap_event *event, void *) -> int {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        ESP_LOGV(TAG, "connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);

        if (event->connect.status == 0) {
            /* Max packet length, min transmission time */
            BLE_LOGI(TAG, "set_data_len(%X, %X)", txDataLength, txDataTime);
            int rc = ble_gap_set_data_len(event->connect.conn_handle,
                                          txDataLength, txDataTime);
            if (rc != 0) {
                ESP_LOGW(TAG, "set_data_len failed; rc=0x%X", rc);
            }

            l_currentCon = event->connect.conn_handle;
        }

        if (event->connect.status != 0) {
            /* Connection failed; resume advertising. */
            advertise();
        } else {
            if (!l_deactivated) {
                /* Connection successful - enforce PIN authentication for all connections */
                ESP_LOGI(TAG, "Connection established - enforcing mandatory PIN");

                vTaskDelay(100 / portTICK_PERIOD_MS);

                int rc = ble_gap_security_initiate(event->connect.conn_handle);
                if (rc != 0) {
                    ESP_LOGW(TAG, "Failed to initiate PIN security: %d", rc);
                    ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                    ESP_LOGW(TAG, "Connection terminated - security enforcement failed");
                } else {
                    BLE_LOGI(TAG, "PIN authentication initiated");
                }
            } else {
                ESP_LOGW(TAG, "Bluetooth PIN deactivated - proceeding without authentication");
            }
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT: {
        ESP_LOGI(TAG, "Device disconnected: reason=%d handle=%d", event->disconnect.reason, event->disconnect.conn.conn_handle);

        // Common disconnect reasons:
        // 0x08 = Connection timeout
        // 0x13 = Remote user terminated connection
        // 0x16 = Connection terminated by local host
        // 0x3D = Connection failed due to authentication failure
#if ZZ_BLE_DEBUG
        const char *reason_str = "";
        switch (event->disconnect.reason) {
        case 0x08:
            reason_str = " (Connection timeout)";
            break;
        case 0x13:
            reason_str = " (Remote user terminated)";
            break;
        case 0x16:
            reason_str = " (Local host terminated)";
            break;
        case 0x3D:
            reason_str = " (Authentication failure)";
            break;
        case 517:
            reason_str = " (NimBLE: Connection timeout during pairing)";
            break;
        case 531:
            reason_str = " (WRONG PIN - Security authentication failed)";
            break;
        default:
            reason_str = " (Unknown reason)";
            break;
        }
        BLE_LOGI(TAG, "Reason: %d%s", event->disconnect.reason, reason_str);
#endif

        if (event->disconnect.conn.conn_handle == l_currentCon) {
            l_currentCon = BLE_HS_CONN_HANDLE_NONE;
        }

        /* Connection terminated; resume advertising. */
        advertise();
        return 0;
    }

    case BLE_GAP_EVENT_ADV_COMPLETE:
        BLE_LOGV(TAG, "advertise complete; reason=%d",
                 event->adv_complete.reason);
        advertise();
        return 0;

    case BLE_GAP_EVENT_MTU:
        BLE_LOGV(TAG, "mtu update event; conn_handle=%d cid=%d mtu=%d",
                 event->mtu.conn_handle,
                 event->mtu.channel_id,
                 event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        BLE_LOGI(TAG, "Security event: passkey action requested");
        return onSecurityEvent(event, nullptr);

    case BLE_GAP_EVENT_ENC_CHANGE:
        BLE_LOGI(TAG, "Security event: encryption/authentication result");
        return onSecurityEvent(event, nullptr);

    case BLE_GAP_EVENT_IDENTITY_RESOLVED:
        BLE_LOGI(TAG, "Security event: previously bonded device connecting");
        return onSecurityEvent(event, nullptr);

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        BLE_LOGI(TAG, "Security event: repeat pairing attempt - deleting old bond and retrying");

        struct ble_gap_conn_desc desc;
        int rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc == 0) {
            /* Remove the stored bond for this peer so it can pair again */
            ble_store_util_delete_peer(&desc.peer_id_addr);
        } else {
            ESP_LOGW(TAG, "repeat_pairing: unable to find conn desc (rc=%d)", rc);
        }

        /* Tell NimBLE to retry pairing now that we removed the old bond */
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
        BLE_LOGI(TAG, "Connection update requested");
        return 0;

    case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
        BLE_LOGI(TAG, "L2CAP update requested");
        return 0;

    default:
        BLE_LOGD(TAG, "Unhandled GAP event: %d", event->type);
        return 0;
    }

    return 0;
}

static auto advertise() -> void {
    if (!l_running) {
        return;
    }

    ble_hs_adv_fields fields{};

    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = defaultPowerLevel;

    static constexpr ble_uuid16_t alertUuid = uuid16_from_str("1811");
    fields.uuids16 = &alertUuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    int rc;
    rc = ble_gap_adv_set_fields(&fields);

    if (rc != 0) {
        ESP_LOGE(TAG, "error setting advertisement data; rc=%d", rc);
        return;
    }

    /* Use up the entire scan response payload for the device name */
    ble_hs_adv_fields scanFields{};
    scanFields.name = reinterpret_cast<const uint8_t *>(l_deviceName.data());
    scanFields.name_len = l_deviceName.length();
    scanFields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&scanFields);

    if (rc != 0) {
        ESP_LOGE(TAG, "error setting scan response data; rc=%d", rc);
        return;
    }

    /* Begin advertising. */
    ble_gap_adv_params advParams{};
    advParams.conn_mode = BLE_GAP_CONN_MODE_UND;
    advParams.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(l_ownAddrType, NULL, BLE_HS_FOREVER,
                           &advParams, onGapEvent, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "error enabling advertisement; rc=%d", rc);
        return;
    }
}

static auto onSecurityEvent(struct ble_gap_event *event, void *) -> int {
    if (l_deactivated) {
        /* Security disabled at runtime; ignore security events */
        return 0;
    }
    switch (event->type) {
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        BLE_LOGI(TAG, "Passkey action: %d", event->passkey.params.action);
        BLE_LOGI(TAG, "Connection handle: %d", event->passkey.conn_handle);

        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            std::uint32_t code;
            bool pinAvailable = false;

            if (getUserPin(code)) {
                pinAvailable = true;
            } else if (getDevPin(code)) {
                pinAvailable = true;
            }

            if (!pinAvailable) {
                ESP_LOGE(TAG, "SECURITY ERROR: No PIN configured! Terminating connection.");
                ble_gap_terminate(event->passkey.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                return 0;
            }

            ESP_LOGI(TAG, "BLE PIN: %06lu", static_cast<unsigned long>(code));

            struct ble_sm_io pkey = {};
            pkey.action = event->passkey.params.action;
            pkey.passkey = code;
            ble_sm_inject_io(event->passkey.conn_handle, &pkey);

        } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
            BLE_LOGW(TAG, "Peer requested INPUT, but device is display-only; ignoring");

        } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            BLE_LOGI(TAG, "Numeric comparison: %06lu", static_cast<unsigned long>(event->passkey.params.numcmp));
            struct ble_sm_io pkey = {};
            pkey.action = event->passkey.params.action;
            pkey.numcmp_accept = 1;
            ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        }
        BLE_LOGI(TAG, "Passkey handling complete");
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            BLE_LOGI(TAG, "PIN authentication successful; connection encrypted");
        } else {
            /* Common case: phone deleted pairing, ESP still has bond. Delete and retry. */
            ESP_LOGW(TAG, "Security failed (status=%d) - deleting peer bond and re-initiating",
                     event->enc_change.status);

            struct ble_gap_conn_desc desc;
            int rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
            if (rc == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
                rc = ble_gap_security_initiate(event->enc_change.conn_handle);
                if (rc != 0) {
                    ESP_LOGW(TAG, "Failed to re-initiate security after bond delete: %d", rc);
                }
            } else {
                ESP_LOGW(TAG, "enc_change: unable to find conn desc (rc=%d)", rc);
            }
        }
        return 0;

    case BLE_GAP_EVENT_IDENTITY_RESOLVED:
        BLE_LOGI(TAG, "Recognized bonded device - auto-connecting securely");
        return 0;

    default:
        BLE_LOGD(TAG, "Other security event: %d", event->type);
        return 0;
    }

    return 0;
}

extern "C" void ble_store_config_init(void);

Task<NIMBLE_HS_STACK_SIZE> hostTask{
    "ble_host",
    Core::PRO,
    []() {
        /* This function will return only when nimble_port_stop() is executed */
        nimble_port_run();

        /* Cleanup */
        nimble_port_deinit();
        Task<>::haltCurrent();
    },
};

static const Ble::Gatts::Service lizardComService{
    serviceUuid,
    {
        Ble::Gatts::Characteristic{
            characteristicUuid,
            BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            [](std::uint16_t conn_handle, std::uint16_t attr_handle, ble_gatt_access_ctxt *ctx) -> int {
                struct ble_gap_conn_desc desc;
                int rc = ble_gap_conn_find(conn_handle, &desc);
                if (rc != 0) {
                    ESP_LOGE(TAG, "Invalid connection handle");
                    return BLE_ATT_ERR_UNLIKELY;
                }

                if (!l_deactivated) {
                    if (!desc.sec_state.encrypted) {
                        ESP_LOGE(TAG, "Unencrypted command attempt - connection not authenticated");
                        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
                    }

                    if (!desc.sec_state.authenticated) {
                        ESP_LOGE(TAG, "Unauthenticated command attempt - missing PIN authentication");
                        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
                    }

                    BLE_LOGI(TAG, "Secure command: encrypted & authenticated connection verified");
                } else {
                    BLE_LOGW(TAG, "Bluetooth PIN deactivated - accepting command without authentication");
                }

                const std::string_view command{reinterpret_cast<char *>(ctx->om->om_data), ctx->om->om_len};
                l_clientCallback(command);

                return 0;
            },
        },
        Ble::Gatts::Characteristic{
            notifyCharaUuid,
            BLE_GATT_CHR_F_NOTIFY,
            &l_notifyCharaValueHandle,
            [](std::uint16_t, std::uint16_t, ble_gatt_access_ctxt *ctx) -> int {
                /* not to be read directly, only accessible via notifications */
                return 0;
            },
        },
    },
};

const std::array services{
    lizardComService.def(),
    ble_gatt_svc_def{},
};

auto init(const std::string_view &deviceName,
          CommandCallback onCommand) -> void {
    l_deviceName = decltype(l_deviceName)(deviceName);
    l_clientCallback = onCommand;
    l_running = true;

    esp_err_t nvs_rc = nvs_flash_init();
    if (nvs_rc == ESP_ERR_NVS_NO_FREE_PAGES || nvs_rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS is full.");
    }

    nimble_port_init();

    /* Initialize persistent storage for NimBLE (required for bonding) */
    ble_store_config_init();

    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = [](int reason) {
        ESP_LOGV(TAG, "Resetting state; reason=%d", reason);
    };

    ble_hs_cfg.sync_cb = []() {
        int rc;

        rc = ble_hs_util_ensure_addr(0);
        assert(rc == 0);

        /* Figure out address to use while advertising (no privacy for now) */
        rc = ble_hs_id_infer_auto(0, &l_ownAddrType);
        if (rc != 0) {
            ESP_LOGE(TAG, "error determining address type; rc=%d", rc);
            return;
        }

        /* Begin advertising. */
        advertise();
    };

    ble_hs_cfg.gatts_register_cb = nullptr;
    ble_hs_cfg.store_status_cb = nullptr;

    /* Configure security manager: enable bonding + MITM, allow SC if supported */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY; // Display-only: we show a passkey
    ble_hs_cfg.sm_bonding = 1;                      // Enable bonding so phones remember pairing
    ble_hs_cfg.sm_mitm = 1;                         // Require MITM (passkey/NumericCompare)
    ble_hs_cfg.sm_sc = 1;                           // Prefer LE Secure Connections if peer supports
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc;

    rc = ble_gatts_count_cfg(services.data());
    assert(rc == 0);

    rc = ble_gatts_add_svcs(services.data());
    assert(rc == 0);

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, defaultPowerLevel);

    /* This device name will be exposed as an attribute as part of GAP,
     * but not within advertisement packets */
    ble_svc_gap_device_name_set(deviceName.data());

    hostTask.run();
}

auto send(const std::string_view &data) -> int {
    if (l_currentCon == BLE_HS_CONN_HANDLE_NONE) {
        return BLE_HS_ENOTCONN;
    }

    os_mbuf *om{ble_hs_mbuf_from_flat(data.data(), data.length())};
    if (om == nullptr) {
        return BLE_HS_ENOMEM;
    }

    return ble_gattc_notify_custom(l_currentCon, l_notifyCharaValueHandle, om);
}

auto fini() -> void {

    if (!l_running) {
        return;
    }
    if (nimble_port_stop() == 0) {
        ESP_ERROR_CHECK(nimble_port_deinit());
    }
    l_running = false;
}

auto deactivate_pin() -> void {
    l_deactivated = true;
    ESP_LOGW(TAG, "Bluetooth security deactivated: PIN/authentication checks are bypassed");
}

auto reset_bonds() -> void {
    /* NimBLE stores bonds and CCCDs using its ble_store implementation
     * which persists to NVS. There is no exported C API to delete-all
     * in the ESP-IDF NimBLE host. As a pragmatic approach, clear the
     * NimBLE store namespaces in NVS. This does not affect other app
     * namespaces (e.g. our Storage). */

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    auto eraseNamespace = [](const char *ns) {
        nvs_handle_t h{};
        esp_err_t e = nvs_open(ns, NVS_READWRITE, &h);
        if (e != ESP_OK)
            return;
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    };

    eraseNamespace("nimble_bond");
    eraseNamespace("nimble_cccd");

    if (l_currentCon != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(l_currentCon, BLE_ERR_REM_USER_CONN_TERM);
    }
}

} // namespace ZZ::BleCommand
