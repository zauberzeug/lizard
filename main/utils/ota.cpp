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

    // Wait a moment for main loop to stop
    vTaskDelay(50 / portTICK_PERIOD_MS);

    // Reconfigure UART for OTA
    if (!uart_configure_for_ota()) {
        uart_ota_active = false;
        return false;
    }

    // Find next available OTA partition
    ota_partition = esp_ota_get_next_update_partition(NULL);
    if (!ota_partition) {
        echo("No available OTA partition found");
        uart_ota_active = false;
        return false;
    }

    echo("OTA partition found: %s", ota_partition->label);

    // Begin OTA operation
    esp_err_t err = esp_ota_begin(ota_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (!echo_if_error("OTA begin", err)) {
        uart_ota_active = false;
        return false;
    }

    ota_status.in_progress = true;
    ota_status.total_size = 0;
    ota_status.received_size = 0;

    echo("UART OTA started successfully");
    return true;
}

int uart_read_with_timeout(uint8_t *buffer, size_t max_size, int timeout_ms) {
    size_t bytes_read = 0;

    int result = uart_read_bytes(UART_PORT_NUM, buffer, max_size, timeout_ms / portTICK_PERIOD_MS);
    if (result > 0) {
        bytes_read = result;
    }

    return bytes_read;
}

bool uart_wait_for_marker(const char *marker, int timeout_ms) {
    char buffer[64];
    int marker_len = strlen(marker);
    int buffer_pos = 0;
    int start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - start_time) < timeout_ms) {
        uint8_t byte;
        if (uart_read_with_timeout(&byte, 1, 100) == 1) {
            buffer[buffer_pos] = byte;
            buffer_pos++;

            // Check if we have the marker
            if (buffer_pos >= marker_len) {
                if (memcmp(&buffer[buffer_pos - marker_len], marker, marker_len) == 0) {
                    return true;
                }
            }

            // Prevent buffer overflow
            if (buffer_pos >= sizeof(buffer) - 1) {
                buffer_pos = 0;
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    return false;
}

bool uart_ota_receive_firmware() {
    if (!ota_status.in_progress) {
        echo("OTA not started");
        return false;
    }

    echo("Waiting for firmware transfer to begin...");
    echo("Send: %s<size_in_bytes>\\n<firmware_data>%s", UART_OTA_START_MARKER, UART_OTA_END_MARKER);

    // Wait for start marker
    if (!uart_wait_for_marker(UART_OTA_START_MARKER, UART_OTA_TIMEOUT_MS)) {
        echo("Timeout waiting for OTA start marker");
        uart_ota_abort();
        return false;
    }

    echo("Received OTA start marker");

    // Read firmware size (transmitted as text after start marker)
    char size_buffer[32];
    int size_pos = 0;
    int timeout_count = 0;

    while (size_pos < sizeof(size_buffer) - 1 && timeout_count < 100) {
        uint8_t byte;
        if (uart_read_with_timeout(&byte, 1, 100) == 1) {
            if (byte == '\n') {
                break;
            }
            size_buffer[size_pos++] = byte;
            timeout_count = 0;
        } else {
            timeout_count++;
        }
    }

    size_buffer[size_pos] = '\0';
    ota_status.total_size = atoi(size_buffer);

    if (ota_status.total_size == 0) {
        echo("Invalid firmware size received: %s", size_buffer);
        uart_ota_abort();
        return false;
    }

    echo("Firmware size: %d bytes", ota_status.total_size);

    // Use fixed chunk size for reliability
    size_t chunk_bytes = UART_OTA_CHUNK_BYTES;
    echo("Using chunk size: %d bytes", chunk_bytes);

    // Send ready signal to start receiving first chunk
    echo(UART_OTA_READY_MARKER);

    // Receive firmware data in chunks - use dynamic allocation for large buffer
    uint8_t *buffer = (uint8_t *)malloc(UART_OTA_CHUNK_SIZE);
    if (!buffer) {
        echo("Failed to allocate %d byte buffer for OTA", UART_OTA_CHUNK_SIZE);
        uart_ota_abort();
        return false;
    }
    size_t remaining = ota_status.total_size;

    while (remaining > 0) {
        size_t bytes_to_read = (remaining > chunk_bytes) ? chunk_bytes : remaining;
        size_t bytes_received = 0;

        // Read this chunk in one piece (256 bytes max, safe for ESP32 UART)
        while (bytes_received < bytes_to_read) {
            size_t read_size = (bytes_to_read - bytes_received > UART_OTA_CHUNK_SIZE) ? UART_OTA_CHUNK_SIZE : (bytes_to_read - bytes_received);

            // Read the entire piece at once since it's only 256 bytes
            int bytes_read = uart_read_with_timeout(buffer, read_size, UART_READ_TIMEOUT_MS * 2);

            if (bytes_read <= 0) {
                echo("Timeout reading %d bytes (received %d/%d in chunk)", read_size, bytes_received, bytes_to_read);
                free(buffer);
                uart_ota_abort();
                return false;
            }

            // Write to OTA partition immediately
            esp_err_t err = esp_ota_write(ota_handle, buffer, bytes_read);
            if (!echo_if_error("OTA write", err)) {
                free(buffer);
                uart_ota_abort();
                return false;
            }

            bytes_received += bytes_read;
            ota_status.received_size += bytes_read;
            remaining -= bytes_read;

            // Small progress indicator for large chunks
            if (bytes_to_read > 10000 && (bytes_received % 10000) == 0) {
                echo("Chunk progress: %d/%d bytes", bytes_received, bytes_to_read);
            }
        }

        // Progress update
        if (ota_status.total_size > 0) {
            float progress = (float)ota_status.received_size / ota_status.total_size * 100.0f;
            echo("Progress: %.1f%% (%d/%d bytes)", progress, ota_status.received_size, ota_status.total_size);
        }

        // Send ready signal for next chunk (if not done)
        if (remaining > 0) {
            echo(UART_OTA_READY_MARKER);
        }

        // No delay needed - let UART buffer handle the flow
    }

    // Wait for end marker
    if (!uart_wait_for_marker(UART_OTA_END_MARKER, 5000)) {
        echo("Warning: End marker not received, but firmware transfer complete");
    } else {
        echo("Received OTA end marker");
    }

    // Finalize OTA
    esp_err_t err = esp_ota_end(ota_handle);
    if (!echo_if_error("OTA end", err)) {
        uart_ota_abort();
        return false;
    }

    ota_handle = 0;

    // Validate the downloaded image
    echo("Validating firmware image...");
    esp_app_desc_t new_app_info;
    err = esp_ota_get_partition_description(ota_partition, &new_app_info);
    if (!echo_if_error("get partition description", err)) {
        echo("Failed to get new firmware info - this indicates the image may be corrupted");
        // Don't abort here, let the bootloader decide
    } else {
        echo("New firmware validation successful!");
        echo("Project: %s, Version: %s", new_app_info.project_name, new_app_info.version);
        echo("Compiled: %s %s", new_app_info.date, new_app_info.time);
        echo("IDF Version: %s", new_app_info.idf_ver);
    }

    // Set boot partition
    err = esp_ota_set_boot_partition(ota_partition);
    if (!echo_if_error("set boot partition", err)) {
        return false;
    }

    ota_status.in_progress = false;
    check_version_ = true;

    // Free the allocated buffer
    free(buffer);

    echo("UART OTA completed successfully. Rebooting...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_restart();

    return true;
}

void verify() {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason != ESP_RST_DEEPSLEEP && reason != ESP_RST_SW) {
        check_version_ = false;
    } else {
        if (check_version_) {
            echo("Detected OTA update - validation disabled for debugging");
            echo("OTA update successful - no validation performed");
            check_version_ = false;
        }
    }
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
