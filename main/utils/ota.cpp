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

    uint32_t loop_count = 0;
    uint32_t wait_cycles = 0;
    const uint32_t MAX_WAIT_CYCLES = 100; // ~200ms at 2ms per cycle

    while (uart_wait_for_data(2000)) {
        size_t available_bytes = 0;
        uart_get_buffered_data_len(UART_PORT_NUM, &available_bytes);

        // Process data if we have enough OR if we've been waiting too long for more data
        bool should_process = false;
        if (available_bytes >= 512) {
            should_process = true;
            wait_cycles = 0; // Reset wait counter
        } else if (available_bytes > 0 && total_bytes_received + available_bytes < ota_partition->size) {
            wait_cycles++;
            if (wait_cycles >= MAX_WAIT_CYCLES) {
                should_process = true;
                wait_cycles = 0;
            } else {
                delay(2);
                // Yield to other tasks periodically
                loop_count++;
                if (loop_count % 50 == 0) {
                    vTaskDelay(1);
                }
                continue;
            }
        } else if (available_bytes > 0) {
            // Last chunk of data
            should_process = true;
        }

        if (!should_process) {
            continue;
        }

        if (available_bytes > 1024) {
            available_bytes = 1024; // Limit to max chunk size
        }

        uint8_t chunk_data[1024] = {0};
        uart_read_bytes(UART_PORT_NUM, chunk_data, available_bytes, 0);

        if (available_bytes > 0) {
            result = esp_ota_write(ota_handle, chunk_data, available_bytes);
            if (result != ESP_OK) {
                echo("OTA write fail [%x]", result);
                esp_ota_abort(ota_handle);
                return false;
            }

            total_bytes_received += available_bytes;

            // Reduce progress spam - only show every 200KB
            if (total_bytes_received % 204800 <= 500) {
                echo("Downloaded %dB", total_bytes_received);
            }
            uart_confirm();

            // Yield to other tasks after processing data
            taskYIELD();
        }
    }

    transfer_end_ms = (esp_timer_get_time() / 1000) - 2000;
    echo("Downloaded %dB in %dms", total_bytes_received, (int32_t)(transfer_end_ms - transfer_start_ms));

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
        2,                       // Priority (low priority)
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
