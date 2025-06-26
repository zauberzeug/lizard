#include "ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "utils/uart.h"
#include "version.h"
#include <atomic>
#include <cstring>
#include <memory>
#include <stdio.h>
#include <string>

#include "driver/uart.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

namespace ota {

#define UART_PORT_NUM UART_NUM_0
#define UART_BUFFER_SIZE 2048
#define UART_READ_TIMEOUT_MS 1000
#define UART_OTA_BUFFER_SIZE (32 * 1024) // 32KB buffer for OTA

static RTC_NOINIT_ATTR bool check_version_;
static uart_ota_status_t ota_status = {0, 0, false};
static esp_ota_handle_t ota_handle = 0;
static const esp_partition_t *ota_partition = nullptr;

// Global flag to disable main UART processing during OTA
bool uart_ota_active = false;

// Save original UART configuration
static QueueHandle_t original_uart_queue = NULL;

bool echo_if_error(const char *message, esp_err_t err) {
    if (err != ESP_OK) {
        const char *error_name = esp_err_to_name(err);
        echo("Error: %s in %s", error_name, message);
        return false;
    }
    return true;
}

void rollback_and_reboot() {
    echo("Rolling back to previous version");
    check_version_ = false;
    echo_if_error("rolling back", esp_ota_mark_app_invalid_rollback_and_reboot());
}

uart_ota_status_t uart_ota_get_status() {
    return ota_status;
}

bool uart_configure_for_ota() {
    echo("Reconfiguring UART for OTA (larger buffer, no pattern detection)");

    // Delete the current UART driver
    if (uart_is_driver_installed(UART_PORT_NUM)) {
        uart_driver_delete(UART_PORT_NUM);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // Configure UART with larger buffer and no interrupts
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };

    esp_err_t err = uart_param_config(UART_PORT_NUM, &uart_config);
    if (!echo_if_error("UART param config for OTA", err)) {
        return false;
    }

    // Install driver with large buffer and no queue for interrupts
    err = uart_driver_install(UART_PORT_NUM, UART_OTA_BUFFER_SIZE, 0, 0, NULL, 0);
    if (!echo_if_error("UART driver install for OTA", err)) {
        return false;
    }

    // Don't set rx timeout - let uart_read_bytes handle its own timeouts

    echo("UART reconfigured successfully for OTA");
    return true;
}

bool uart_restore_normal_config() {
    echo("Restoring normal UART configuration");

    // Delete OTA UART configuration
    if (uart_is_driver_installed(UART_PORT_NUM)) {
        uart_driver_delete(UART_PORT_NUM);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // Restore original configuration (matching main.cpp)
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };

    esp_err_t err = uart_param_config(UART_PORT_NUM, &uart_config);
    if (!echo_if_error("UART param config restore", err)) {
        return false;
    }

    // Restore with original buffer size and pattern detection
    err = uart_driver_install(UART_PORT_NUM, 1024 * 8, 0, 20, &original_uart_queue, 0);
    if (!echo_if_error("UART driver install restore", err)) {
        return false;
    }

    // Re-enable pattern detection for newlines
    uart_enable_pattern_det_baud_intr(UART_PORT_NUM, '\n', 1, 9, 0, 0);
    uart_pattern_queue_reset(UART_PORT_NUM, 100);

    echo("UART configuration restored successfully");
    return true;
}

void uart_ota_abort() {
    if (ota_status.in_progress) {
        echo("Aborting UART OTA");
        if (ota_handle) {
            esp_ota_abort(ota_handle);
            ota_handle = 0;
        }
        ota_status.in_progress = false;
        ota_status.total_size = 0;
        ota_status.received_size = 0;

        // Restore normal UART configuration
        uart_restore_normal_config();

        uart_ota_active = false;
        echo("Main UART processing re-enabled");
    }
}

bool uart_ota_start() {
    echo("Starting UART OTA process");

    // Disable main UART processing during OTA
    uart_ota_active = true;
    echo("Main UART processing disabled for OTA");

    ota_status.in_progress = true;
    ota_status.total_size = 0;
    ota_status.received_size = 0;

    echo("UART OTA started successfully");
    return true;
}

// Simple wait function - returns 1 if data available, 0 if timeout
int8_t uart_wait_for_data(uint16_t time) {
    int64_t th = esp_timer_get_time();
    size_t avl = 0;

    while (esp_timer_get_time() - th < time * 1000) {
        uart_get_buffered_data_len(UART_PORT_NUM, &avl);
        if (avl > 0) {
            return 1;
        }
    }
    return 0;
}

// Simple confirm - send \r\n
void uart_confirm() {
    uart_write_bytes(UART_PORT_NUM, "\r\n", 2);
}

bool uart_ota_receive_firmware() {
    if (!ota_status.in_progress) {
        echo("OTA not started");
        return false;
    }

    echo("Ready for firmware download - send data now");

    // Ultra-simple download - exactly like the fast implementation
    esp_err_t err;
    int64_t t1 = 0, t2 = 0;
    uint32_t total = 0;
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *ota_partition = NULL;

    ota_partition = esp_ota_get_next_update_partition(NULL);
    if (!ota_partition) {
        echo("No available OTA partition found");
        return false;
    }

    err = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        echo("OTA begin fail [0x%x]", err);
        return false;
    }

    t1 = esp_timer_get_time() / 1000;

    // Main download loop - send initial ready signal first
    uart_confirm(); // Send initial \r\n to confirm ready for first chunk

    while (uart_wait_for_data(2000)) {
        size_t avl = 0;
        uart_get_buffered_data_len(UART_PORT_NUM, &avl);

        if (avl > 1024) {
            avl = 1024; // Limit to max chunk size
        }

        total += avl;
        uint8_t data[1024] = {0};
        uart_read_bytes(UART_PORT_NUM, data, avl, 0);

        if (avl > 0) {
            err = esp_ota_write(ota_handle, data, avl);
            if (err != ESP_OK) {
                echo("OTA write fail [%x]", err);
                esp_ota_abort(ota_handle);
                return false;
            }

            // Reduce progress spam - only show every 200KB
            if (total % 204800 <= 500) {
                echo("Downloaded %dB", total);
            }

            // Send ready signal for next chunk after processing current one
            uart_confirm();
        }
    }

    t2 = (esp_timer_get_time() / 1000) - 2000;
    echo("Downloaded %dB in %dms", total, (int32_t)(t2 - t1));

    // Finalize OTA
    err = esp_ota_end(ota_handle);
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(ota_partition);
        if (err == ESP_OK) {
            echo("OTA OK, restarting...");
            ota_status.in_progress = false;
            ota_status.received_size = total;

            // Brief delay to ensure message is sent before restart
            vTaskDelay(500 / portTICK_PERIOD_MS);
            esp_restart();
        } else {
            echo("OTA set boot partition fail [0x%x]", err);
            return false;
        }
    } else {
        echo("OTA end fail [0x%x]", err);
        return false;
    }

    return true;
}

void verify() {
    // Simplified verify - no complex validation
    echo("OTA verify complete");
}

bool version_checker() {
    // Send validation request over UART and wait for response
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (!running_partition) {
        echo("Could not get running partition");
        return false;
    }

    echo("Running from partition: %s", running_partition->label);
    echo("Current version: %s", GIT_VERSION);

    // Send validation request
    echo("OTA_VALIDATE:%s", GIT_VERSION);

    // Wait for validation response from host
    char response_buffer[64];
    int pos = 0;
    int timeout_ms = 10000; // 10 second timeout
    int start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - start_time) < timeout_ms) {
        int available = uart_read_bytes(UART_NUM_0, (uint8_t *)&response_buffer[pos], 1, 100 / portTICK_PERIOD_MS);
        if (available > 0) {
            if (response_buffer[pos] == '\n') {
                response_buffer[pos] = '\0';

                if (strstr(response_buffer, "OTA_VALID") != NULL) {
                    echo("OTA validation successful");
                    return true;
                } else if (strstr(response_buffer, "OTA_INVALID") != NULL) {
                    echo("OTA validation failed");
                    return false;
                }
                pos = 0; // Reset for next line
            } else if (pos < sizeof(response_buffer) - 2) {
                pos++;
            } else {
                pos = 0; // Buffer full, reset
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    echo("OTA validation timeout - no response from host");
    return false;
}

void verify_task(void *pvParameters) {
    ota::verify();
    vTaskDelete(NULL);
}

void uart_ota_task(void *pvParameters) {
    echo("Starting UART OTA task");

    if (!uart_ota_start()) {
        echo("Failed to start UART OTA");
        vTaskDelete(NULL);
        return;
    }

    if (!uart_ota_receive_firmware()) {
        echo("UART OTA failed");
    }

    vTaskDelete(NULL);
}

} // namespace ota
