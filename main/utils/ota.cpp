#include "ota.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_zeug/eventhandler.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "utils/uart.h"
#include "version.h"
#include <atomic>
#include <cstring>
#include <memory>
#include <stdio.h>
#include <string>
#include <vector>

#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"

namespace ota {

#define MAX_HTTP_BUFFER_SIZE 1024

static RTC_NOINIT_ATTR bool check_version_;
static RTC_NOINIT_ATTR char ssid_[32];
static RTC_NOINIT_ATTR char password_[32];
static RTC_NOINIT_ATTR char url_[128];

int retry_num_ = 0;
int max_retry_num_ = 10;
const char *verify_path = "/verify";

static size_t total_content_length_ = 0;
static size_t received_length_ = 0;

static SemaphoreHandle_t wifi_semaphore = NULL;
static const TickType_t semaphore_timeout = 60 * 1000 / portTICK_PERIOD_MS;

bool echo_if_error(const char *message, esp_err_t err) {
    if (err != ESP_OK) {
        const char *error_name = esp_err_to_name(err);
        echo("Error: %s in %s\n", error_name, message);
        return false;
    }
    return true;
}

bool is_wifi_connected() {
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    return err == ESP_OK;
}

char *append_verify_to_url(const char *verify_path) {
    size_t remaining_size = sizeof(url_) - strlen(url_) - 1;
    char *output = url_;

    if (remaining_size >= strlen(verify_path)) {
        strncat(output, verify_path, remaining_size);
    } else {
        echo("URL too long to append verify path");
    }

    return output;
}

void rollback_and_reboot() {
    echo("Rolling back to previous version");
    check_version_ = false;
    memset(ssid_, 0, sizeof(ssid_));
    memset(password_, 0, sizeof(password_));
    memset(url_, 0, sizeof(url_));
    echo_if_error("stopping wifi", esp_wifi_stop());
    echo_if_error("rolling back", esp_ota_mark_app_invalid_rollback_and_reboot());
}

void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    switch (event_id) {
    case WIFI_EVENT_STA_START:
        echo("WiFi started\n");
        break;
    case WIFI_EVENT_STA_CONNECTED:
        echo("WiFi connected\n");
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        echo("WiFi lost connection\n");
        if (++retry_num_ <= max_retry_num_) {
            echo("Retrying to Connect... (%d/%d)\n", retry_num_, max_retry_num_);
            esp_wifi_connect();
        } else {
            echo("Max retry attempts reached, stopping WiFi attempts.\n");
            xSemaphoreGive(wifi_semaphore);
        }
        break;
    }
    case IP_EVENT_STA_GOT_IP:
        echo("WiFi got IP...\n");
        xSemaphoreGive(wifi_semaphore);
        break;
    case WIFI_EVENT_STA_STOP:
        echo("WiFi stopped\n");
        break;
    default:
        echo("Unhandled WiFi Event: %d\n", event_id);
        break;
    }
}

esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        echo("HTTP_EVENT_ERROR\n");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        echo("HTTP_EVENT_ON_CONNECTED\n");
        break;
    case HTTP_EVENT_HEADER_SENT:
        echo("HTTP_EVENT_HEADER_SENT\n");
        break;
    case HTTP_EVENT_ON_HEADER:
        echo("Header %s: %s\n", evt->header_key, evt->header_value);
        if (strcasecmp(evt->header_key, "Content-Length") == 0) {
            total_content_length_ = strtoul(evt->header_value, NULL, 10);
            echo("Total content length set: %d bytes\n", total_content_length_);
        }
        break;
    case HTTP_EVENT_ON_DATA:
        received_length_ += evt->data_len;
        if (evt->data_len > 0) {
            echo("Received data (%d bytes), Total received: %d bytes", evt->data_len, received_length_);
            if (total_content_length_ > 0) {
                double percentage = (double)received_length_ / total_content_length_ * 100;
                echo("Progress: %.2f%%\n", percentage);
            }
        }
        if (received_length_ == total_content_length_) {
            echo("All content received. Total size: %d bytes\n", total_content_length_);
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        echo("HTTP_EVENT_ON_FINISH\n");
        received_length_ = 0;
        total_content_length_ = 0;
        break;
    case HTTP_EVENT_DISCONNECTED:
        echo("HTTP_EVENT_DISCONNECTED\n");
        break;
    default:
        echo("Unhandled HTTP Event\n");
        break;
    }
    return ESP_OK;
}

bool setup_wifi(const char *ssid, const char *password) {
    retry_num_ = 0;
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);

    if (err == ESP_ERR_WIFI_NOT_INIT) {
        echo("WiFi not initialized");
        if (!echo_if_error("netif init", esp_netif_init())) {
            return false;
        }

        esp_err_t loop_error = esp_event_loop_create_default();
        if (loop_error != ESP_OK && loop_error != ESP_ERR_INVALID_STATE) {
            echo_if_error("event loop create", loop_error);
            return false;
        }

        esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        if (!echo_if_error("wifi init", esp_wifi_init(&cfg)) ||
            !echo_if_error("wifi event handler register", esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL)) ||
            !echo_if_error("ip event handler register", esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL))) {
            return false;
        }
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    wifi_config_t wifi_config{};
    strcpy((char *)wifi_config.sta.ssid, ssid);
    strcpy((char *)wifi_config.sta.password, password);

    if (!echo_if_error("wifi set mode", esp_wifi_set_mode(WIFI_MODE_STA)) ||
        !echo_if_error("wifi set config", esp_wifi_set_config(WIFI_IF_STA, &wifi_config)) ||
        !echo_if_error("wifi start", esp_wifi_start()) ||
        !echo_if_error("wifi connect", esp_wifi_connect())) {
        return false;
    }

    return true;
}

void attempt(const char *url) {
    esp_http_client_config_t config{};
    esp_err_t err;
    config.skip_cert_common_name_check = true;
    config.keep_alive_enable = true;
    config.url = url;
    config.buffer_size = 1024;
    config.timeout_ms = 10 * 1000;
    config.event_handler = http_event_handler;

    total_content_length_ = 0;
    received_length_ = 0;

    echo("OTA Attempting to connect to %s", url);
    err = esp_https_ota(&config);
    if (err == ESP_OK) {
        check_version_ = true;
        echo_if_error("stopping wifi", esp_wifi_stop());
        echo("OTA Successful. Rebooting");
        esp_restart();
    } else {
        echo("OTA Failed. Check HTTP output for more information");
    }
}

void verify() {
    bool rollback = false;
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason != ESP_RST_DEEPSLEEP && reason != ESP_RST_SW) {
        check_version_ = false;
        memset(ssid_, 0, sizeof(ssid_));
        memset(password_, 0, sizeof(password_));
        memset(url_, 0, sizeof(url_));
    } else {
        if (check_version_) {
            echo("Detected ota update, checking version");

            if (!is_wifi_connected()) {
                echo("Not connected to WiFi, connecting");
                wifi_semaphore = xSemaphoreCreateBinary();
                if (!setup_wifi(ssid_, password_)) {
                    echo("Failed to connect to WiFi. Rolling back");
                    rollback = true;
                } else {
                    if (xSemaphoreTake(wifi_semaphore, semaphore_timeout) == pdTRUE) {
                        if (!version_checker(append_verify_to_url(verify_path))) {
                            echo("Problem with version check. Rolling back");
                            rollback = true;
                        }
                    } else {
                        echo("Failed to connect to WiFi. Rolling back");
                        rollback = true;
                    }
                }
                vSemaphoreDelete(wifi_semaphore);
            }

            check_version_ = false;
            memset(ssid_, 0, sizeof(ssid_));
            memset(password_, 0, sizeof(password_));
            memset(url_, 0, sizeof(url_));

            if (rollback) {
                rollback_and_reboot();
            } else {
                echo_if_error("stopping wifi", esp_wifi_stop());
                echo("Version check complete. OTA was successful");
            }
        }
    }
}

bool version_checker(const char *url) {
    char output_buffer[MAX_HTTP_BUFFER_SIZE + 1] = {0};
    int content_length = 0;
    esp_http_client_config_t config{};
    config.url = url;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        echo("Failed to initialize HTTP client");
        return false;
    }

    if (!echo_if_error("HTTP set method", esp_http_client_set_method(client, HTTP_METHOD_GET))) {
        esp_http_client_cleanup(client);
        return false;
    }

    if (!echo_if_error("HTTP open", esp_http_client_open(client, 0))) {
        esp_http_client_cleanup(client);
        return false;
    }

    content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0) {
        echo("Invalid content length: %d", content_length);
        esp_http_client_cleanup(client);
        return false;
    }

    int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_BUFFER_SIZE);
    if (data_read < 0) {
        echo("Failed to read data");
        esp_http_client_cleanup(client);
        return false;
    }

    esp_http_client_close(client);
    std::string version(output_buffer);

    // shorten long git tags like "v1.2.3-123-g12345678" to "v1.2.3"
    std::string git_version = GIT_VERSION;
    size_t git_version_short = git_version.find('-');
    if (git_version_short != std::string::npos) {
        git_version = git_version.substr(0, git_version_short);
    }

    if (version != GIT_VERSION) {
        echo(version == "Unknown version" ? "Unknown version, but connection successful" : "Found different version: %s. But connection successful", version.c_str());
    } else {
        echo("Version matches: %s", version.c_str());
    }

    esp_http_client_cleanup(client);
    return true;
}

void verify_task(void *pvParameters) {
    ota::verify();
    vTaskDelete(NULL);
}

void ota_task(void *pvParameters) {
    ota_params_t *params = static_cast<ota_params_t *>(pvParameters);

    strcpy(ssid_, params->ssid.c_str());
    strcpy(password_, params->password.c_str());
    strcpy(url_, params->url.c_str());

    echo("Starting OTA task");
    if (is_wifi_connected()) {
        echo("Already connected to WiFi");
        attempt(params->url.c_str());
    } else {
        echo("Not connected to WiFi, setting up WiFi");
        wifi_semaphore = xSemaphoreCreateBinary();

        if (!setup_wifi(params->ssid.c_str(), params->password.c_str())) {
            echo("Failed to connect to WiFi during OTA");
        } else {
            if (xSemaphoreTake(wifi_semaphore, semaphore_timeout) == pdTRUE) {
                attempt(params->url.c_str());
            } else {
                echo("Failed to connect to WiFi within the time period");
            }
        }
        vSemaphoreDelete(wifi_semaphore);
    }

    delete params;
    vTaskDelete(NULL);
}

} // namespace ota