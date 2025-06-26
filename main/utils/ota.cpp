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

bool uart_ota_start() {
    echo("Starting UART OTA process");

    echo("UART OTA started successfully");
    return true;
}

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
    echo("Ready for firmware download - send data now");

    // Ultra-simple download - exactly like the fast implementation
    esp_err_t err;
    int64_t t1 = 0, t2 = 0;
    uint32_t total = 0;
    esp_ota_handle_t ota_handle = 0;

    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
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

    uart_confirm();

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
} // namespace ota
