#include "ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "utils/uart.h"
#include "version.h"
#include <cstring>
#include <stdio.h>
#include <string>

#include "driver/uart.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "timing.h"
#include <algorithm>

namespace ota {

#define UART_PORT_NUM UART_NUM_0

// Task management variables
static TaskHandle_t ota_bridge_task_handle = nullptr;
static volatile bool ota_bridge_should_stop = false;

// Global confirm index for debugging
static uint32_t confirm_index = 0;

bool echo_if_error(const char *message, esp_err_t err) {
    if (err != ESP_OK) {
        const char *error_name = esp_err_to_name(err);
        echo("Error: %s in %s", error_name, message);
        return false;
    }
    return true;
}

// rollback_and_reboot() function removed - unused

int8_t uart_wait_for_data(uint16_t timeout_ms) {
    int64_t start_time = esp_timer_get_time();
    size_t available_bytes = 0;

    while (esp_timer_get_time() - start_time < timeout_ms * 1000) {
        uart_get_buffered_data_len(UART_PORT_NUM, &available_bytes);
        if (available_bytes > 0) {
            return 1;
        }
        // Yield to other tasks to prevent watchdog timeout
        vTaskDelay(1); // 1ms delay
    }
    return 0;
}

// Simple confirm - send index\r\n
void uart_confirm() {
    confirm_index++;

    char confirm_msg[16];
    int len = snprintf(confirm_msg, sizeof(confirm_msg), "%lu\r\n", confirm_index);
    uart_write_bytes(UART_PORT_NUM, confirm_msg, len);
}

bool uart_ota_receive_firmware() {
    // Reset confirm index for new OTA session
    confirm_index = 0;

    echo("Starting UART OTA process");
    echo("Ready for firmware download");

    // Ultra-simple download - exactly like the fast implementation
    esp_err_t result;
    int64_t transfer_start_ms = 0, transfer_end_ms = 0;
    uint32_t total_bytes_received = 0;
    esp_ota_handle_t ota_handle = 0;

    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    if (!ota_partition) {
        echo("No available OTA partition found");
        return false;
    }

    result = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (result != ESP_OK) {
        echo("OTA begin fail [0x%x]", result);
        return false;
    }

    transfer_start_ms = esp_timer_get_time() / 1000;

    // Discard any pending command bytes (e.g., "core.ota()\n") before signalling readiness
    uart_flush_input(UART_PORT_NUM);

    uart_confirm();

    uint8_t chunk_buffer[2048] = {0}; // Larger buffer for dynamic chunks
    uint32_t buffer_pos = 0;
    uint32_t chunk_count = 0;

    // Helper function to calculate XOR checksum
    auto calculate_checksum = [](const uint8_t *data, size_t len) -> uint8_t {
        uint8_t checksum = 0;
        for (size_t i = 0; i < len; i++) {
            checksum ^= data[i];
        }
        return checksum;
    };

    while (true) {
        // Wait for data with timeout
        if (!uart_wait_for_data(5000)) {
            echo("Timeout waiting for data, finishing transfer");
            break;
        }

        // Read available bytes
        size_t available = 0;
        uart_get_buffered_data_len(UART_PORT_NUM, &available);

        if (available == 0) {
            continue;
        }

        // Read into our buffer, but don't overflow
        size_t can_read = sizeof(chunk_buffer) - buffer_pos - 1; // Leave space for null terminator
        if (can_read > available) {
            can_read = available;
        }

        if (can_read == 0) {
            echo("Buffer full without finding checksum delimiter, aborting");
            esp_ota_abort(ota_handle);
            return false;
        }

        int bytes_read = uart_read_bytes(UART_PORT_NUM, chunk_buffer + buffer_pos, can_read, 0);
        if (bytes_read <= 0) {
            continue;
        }

        buffer_pos += bytes_read;
        chunk_buffer[buffer_pos] = 0; // Null terminate for string operations

        // Look for checksum delimiter pattern '\r\n@'
        char *delimiter_start = nullptr;
        for (size_t i = 0; i <= buffer_pos - 3; i++) {
            if (chunk_buffer[i] == '\r' && chunk_buffer[i + 1] == '\n' && chunk_buffer[i + 2] == '@') {
                delimiter_start = (char *)&chunk_buffer[i];
                break;
            }
        }

        while (delimiter_start != nullptr) {
            size_t chunk_data_len = delimiter_start - (char *)chunk_buffer;

            // Need: data + '\r\n@' + 2 hex digits + '\r\n' = data + 7 bytes total
            size_t total_needed = chunk_data_len + 7;

            if (buffer_pos >= total_needed) {
                // Extract checksum from hex string (skip \r\n@)
                char checksum_str[3] = {delimiter_start[3], delimiter_start[4], 0};
                uint8_t expected_checksum = (uint8_t)strtol(checksum_str, nullptr, 16);

                // Calculate actual checksum
                uint8_t actual_checksum = calculate_checksum(chunk_buffer, chunk_data_len);

                // Debug: print first chunk info
                if (chunk_count == 0) {
                    echo("DEBUG: First chunk %d bytes, expected: %02x, actual: %02x",
                         chunk_data_len, expected_checksum, actual_checksum);
                    echo("DEBUG: Checksum string: '%s'", checksum_str);
                    echo("DEBUG: First few bytes: %02x %02x %02x %02x",
                         chunk_buffer[0], chunk_buffer[1], chunk_buffer[2], chunk_buffer[3]);
                }

                if (actual_checksum == expected_checksum) {
                    // Checksum valid - write chunk to flash
                    result = esp_ota_write(ota_handle, chunk_buffer, chunk_data_len);
                    if (result != ESP_OK) {
                        echo("OTA write fail [%x]", result);
                        esp_ota_abort(ota_handle);
                        return false;
                    }

                    total_bytes_received += chunk_data_len;
                    chunk_count++;

                    // Progress reporting every 200KB
                    static uint32_t last_reported_kb = 0;
                    uint32_t current_kb = total_bytes_received / 204800;
                    if (current_kb > last_reported_kb) {
                        echo("Downloaded %dB (%d chunks)", total_bytes_received, chunk_count);
                        last_reported_kb = current_kb;
                    }

                    uart_confirm();
                } else {
                    echo("Checksum mismatch: expected %02x, got %02x", expected_checksum, actual_checksum);
                    esp_ota_abort(ota_handle);
                    return false;
                }

                // Remove processed chunk from buffer
                size_t remaining = buffer_pos - total_needed;
                if (remaining > 0) {
                    memmove(chunk_buffer, chunk_buffer + total_needed, remaining);
                }
                buffer_pos = remaining;
                chunk_buffer[buffer_pos] = 0;

                // Look for next delimiter in remaining data
                delimiter_start = nullptr;
                for (size_t i = 0; i <= buffer_pos - 3; i++) {
                    if (chunk_buffer[i] == '\r' && chunk_buffer[i + 1] == '\n' && chunk_buffer[i + 2] == '@') {
                        delimiter_start = (char *)&chunk_buffer[i];
                        break;
                    }
                }

                taskYIELD(); // Allow other tasks to run
            } else {
                // Need more data for complete checksum
                break;
            }
        }
    }

    transfer_end_ms = (esp_timer_get_time() / 1000) - 5000;
    echo("Downloaded %dB in %dms (%d chunks)", total_bytes_received, (int32_t)(transfer_end_ms - transfer_start_ms), chunk_count);

    // Add delay before finalizing to ensure all flash operations are complete
    echo("Waiting for flash operations to complete...");
    vTaskDelay(1000 / portTICK_PERIOD_MS); // 1 second delay

    // Finalize OTA
    result = esp_ota_end(ota_handle);
    if (result == ESP_OK) {
        result = esp_ota_set_boot_partition(ota_partition);
        if (result == ESP_OK) {
            echo("OTA OK, restarting...");

            // Brief delay to ensure message is sent before restart
            vTaskDelay(500 / portTICK_PERIOD_MS);
            esp_restart();
        } else {
            echo("OTA set boot partition fail [0x%x]", result);
            return false;
        }
    } else {
        echo("OTA end fail [0x%x]", result);
        return false;
    }

    return true;
}

bool uart_ota_bridge() {
    // Transparent bridge between UART0 (upstream/PC) and UART1 (downstream p0 ESP)
    // Sends core.ota() to downstream, then pipes traffic both ways until idle.
    constexpr uart_port_t UPSTREAM = UART_NUM_0;
    constexpr uart_port_t DOWNSTREAM = UART_NUM_1;
    constexpr int64_t IDLE_TIMEOUT_MS = 10000; // finish when no traffic for this long

    echo("Starting downstream OTA bridge");
    uart_write_bytes(DOWNSTREAM, "core.ota()\n", 11);

    unsigned long last_data = millis();
    uint32_t loop_count = 0;

    while (true) {
        // Check if we should stop the bridge
        if (ota_bridge_should_stop) {
            echo("Bridge stop requested");
            break;
        }

        bool data_transferred = false;

        /* upstream → downstream */
        size_t avail = 0;
        uart_get_buffered_data_len(UPSTREAM, &avail);
        if (avail) {
            uint8_t buf[1024];
            int n = uart_read_bytes(UPSTREAM, buf, std::min<size_t>(avail, sizeof buf), 0);
            if (n > 0) {
                uart_write_bytes(DOWNSTREAM, buf, n);
                last_data = millis();
                data_transferred = true;
            }
        }

        /* downstream → upstream */
        uart_get_buffered_data_len(DOWNSTREAM, &avail);
        if (avail) {
            uint8_t buf[1024];
            int n = uart_read_bytes(DOWNSTREAM, buf, std::min<size_t>(avail, sizeof buf), 0);
            if (n > 0) {
                uart_write_bytes(UPSTREAM, buf, n);
                last_data = millis();
                data_transferred = true;
            }
        }

        if (millis_since(last_data) > IDLE_TIMEOUT_MS) {
            echo("last_data: %d. IDLE_TIMEOUT_MS: %d. millis_since: %d", last_data, IDLE_TIMEOUT_MS, millis_since(last_data));
            echo("Bridge idle – leaving bridge mode");
            break;
        }

        // Always yield to allow other tasks to run, especially IDLE task
        taskYIELD();

        // Every 100 loops or if no data, add a small delay to prevent CPU starvation
        loop_count++;
        if (!data_transferred || (loop_count % 100 == 0)) {
            vTaskDelay(1);        // 1ms delay to let other tasks run
            esp_task_wdt_reset(); // Reset watchdog periodically
        }
    }

    return true;
}

// Task wrapper function for the bridge
static void ota_bridge_task(void *parameter) {
    echo("OTA bridge task started on core %d", xPortGetCoreID());

    // Add this task to the watchdog timer
    esp_task_wdt_add(NULL);

    // Run the bridge functionality
    uart_ota_bridge();

    echo("OTA bridge task completed");

    // Remove from watchdog timer
    esp_task_wdt_delete(NULL);

    // Clean up task handle
    ota_bridge_task_handle = nullptr;

    // Delete this task
    vTaskDelete(NULL);
}

void start_ota_bridge_task() {
    if (ota_bridge_task_handle != nullptr) {
        echo("OTA bridge task already running");
        return;
    }

    ota_bridge_should_stop = false;

    BaseType_t result = xTaskCreatePinnedToCore(
        ota_bridge_task,         // Task function
        "ota_bridge",            // Task name
        8192,                    // Stack size (8KB for safety)
        NULL,                    // Parameters
        5,                       // Priority (higher priority - was 2)
        &ota_bridge_task_handle, // Task handle
        1                        // Core 1
    );

    if (result != pdPASS) {
        echo("Failed to create OTA bridge task");
        ota_bridge_task_handle = nullptr;
    } else {
        echo("OTA bridge task created on core 1");
    }
}

void stop_ota_bridge_task() {
    if (ota_bridge_task_handle == nullptr) {
        echo("No OTA bridge task to stop");
        return;
    }

    ota_bridge_should_stop = true;

    // Wait for task to finish (with timeout)
    int timeout_count = 0;
    while (ota_bridge_task_handle != nullptr && timeout_count < 100) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        timeout_count++;
    }

    if (ota_bridge_task_handle != nullptr) {
        echo("Force deleting OTA bridge task");
        vTaskDelete(ota_bridge_task_handle);
        ota_bridge_task_handle = nullptr;
    }

    echo("OTA bridge task stopped");
}

bool is_ota_bridge_running() {
    return ota_bridge_task_handle != nullptr;
}

} // namespace ota
