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
#include "esp_timer.h"

namespace ota {

#define UART_PORT_NUM UART_NUM_0

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
    }
    return 0;
}

// Simple confirm - send \r\n
void uart_confirm() {
    uart_write_bytes(UART_PORT_NUM, "\r\n", 2);
}

bool uart_ota_receive_firmware() {
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

    uart_confirm();

    while (uart_wait_for_data(2000)) {
        size_t available_bytes = 0;
        uart_get_buffered_data_len(UART_PORT_NUM, &available_bytes);

        if (available_bytes > 1024) {
            available_bytes = 1024; // Limit to max chunk size
        }

        total_bytes_received += available_bytes;
        uint8_t chunk_data[1024] = {0};
        uart_read_bytes(UART_PORT_NUM, chunk_data, available_bytes, 0);

        if (available_bytes > 0) {
            result = esp_ota_write(ota_handle, chunk_data, available_bytes);
            if (result != ESP_OK) {
                echo("OTA write fail [%x]", result);
                esp_ota_abort(ota_handle);
                return false;
            }

            // Reduce progress spam - only show every 200KB
            if (total_bytes_received % 204800 <= 500) {
                echo("Downloaded %dB", total_bytes_received);
            }

            uart_confirm();
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
} // namespace ota
