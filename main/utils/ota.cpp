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

#include "../global.h"
#include "../modules/proxy.h"
#include "addressing.h"

namespace ota {

#define UART_PORT_NUM UART_NUM_0

static TaskHandle_t ota_bridge_task_handle = nullptr;
static volatile bool ota_bridge_should_stop = false;
static uint32_t confirm_index = 0; // number of confirmations, not chunks received
static uart_port_t bridge_upstream_port = UART_NUM_0;
static uart_port_t bridge_downstream_port = UART_NUM_1;
static uint32_t transfer_timeout_ms = 8000;

bool echo_if_error(const char *message, esp_err_t err) {
    if (err != ESP_OK) {
        const char *error_name = esp_err_to_name(err);
        echo("Error: %s in %s", error_name, message);
        return false;
    }
    return true;
}

int8_t wait_for_uart_data(uint16_t timeout_ms) {
    int64_t start_time = esp_timer_get_time();
    size_t available_bytes = 0;

    while (esp_timer_get_time() - start_time < timeout_ms * 1000) {
        uart_get_buffered_data_len(UART_PORT_NUM, &available_bytes);
        if (available_bytes > 0) {
            return 1;
        }
        vTaskDelay(1);
    }
    return 0;
}

void send_uart_confirmation() {
    confirm_index++;

    char confirm_msg[16];
    int len = snprintf(confirm_msg, sizeof(confirm_msg), "%lu\r\n", confirm_index);
    uart_write_bytes(UART_PORT_NUM, confirm_msg, len);
}

bool receive_firmware_via_uart() {
    confirm_index = 0;

    echo("Starting UART OTA process");
    echo("Ready for firmware download");

    esp_err_t result;
    int64_t transfer_start_ms = 0, transfer_end_ms = 0;
    uint32_t total_bytes_received = 0;
    esp_ota_handle_t ota_handle = 0;

    if (get_uart_external_mode()) {
        echo("External mode detected, deactivating");
        deactivate_uart_external_mode(); // just deactivate it, this is a single plexus solution only
    }

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
    uart_flush_input(UART_PORT_NUM);
    send_uart_confirmation();

    uint8_t chunk_buffer[2048] = {0};
    uint32_t buffer_pos = 0;
    uint32_t chunk_count = 0;

    auto calculate_checksum = [](const uint8_t *data, size_t len) -> uint8_t {
        uint8_t checksum = 0;
        for (size_t i = 0; i < len; i++) {
            checksum ^= data[i];
        }
        return checksum;
    };

    while (true) {
        if (!wait_for_uart_data(transfer_timeout_ms)) {
            echo("Timeout waiting for data, finishing transfer");
            break;
        }

        size_t available = 0;
        uart_get_buffered_data_len(UART_PORT_NUM, &available);

        if (available == 0) {
            continue;
        }

        size_t can_read = sizeof(chunk_buffer) - buffer_pos - 1;
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
        chunk_buffer[buffer_pos] = 0;

        char *delimiter_start = nullptr;
        for (size_t i = 0; i <= buffer_pos - 3; i++) {
            if (chunk_buffer[i] == '\r' && chunk_buffer[i + 1] == '\n' && chunk_buffer[i + 2] == '@') {
                delimiter_start = (char *)&chunk_buffer[i];
                break;
            }
        }

        while (delimiter_start != nullptr) {
            size_t chunk_data_len = delimiter_start - (char *)chunk_buffer;
            size_t total_needed = chunk_data_len + 7;

            if (buffer_pos >= total_needed) {
                char checksum_str[3] = {delimiter_start[3], delimiter_start[4], 0};
                uint8_t expected_checksum = (uint8_t)strtol(checksum_str, nullptr, 16);
                uint8_t actual_checksum = calculate_checksum(chunk_buffer, chunk_data_len);

                if (actual_checksum == expected_checksum) {
                    result = esp_ota_write(ota_handle, chunk_buffer, chunk_data_len);
                    if (result != ESP_OK) {
                        echo("OTA write fail [%x]", result);
                        esp_ota_abort(ota_handle);
                        return false;
                    }

                    total_bytes_received += chunk_data_len;
                    chunk_count++;

                    send_uart_confirmation();
                } else {
                    echo("Checksum mismatch: expected %02x, got %02x", expected_checksum, actual_checksum);
                    esp_ota_abort(ota_handle);
                    return false;
                }

                size_t remaining = buffer_pos - total_needed;
                if (remaining > 0) {
                    memmove(chunk_buffer, chunk_buffer + total_needed, remaining);
                }
                buffer_pos = remaining;
                chunk_buffer[buffer_pos] = 0;

                delimiter_start = nullptr;
                for (size_t i = 0; i <= buffer_pos - 3; i++) {
                    if (chunk_buffer[i] == '\r' && chunk_buffer[i + 1] == '\n' && chunk_buffer[i + 2] == '@') {
                        delimiter_start = (char *)&chunk_buffer[i];
                        break;
                    }
                }

                taskYIELD();
            } else {
                break;
            }
        }
    }

    transfer_end_ms = (esp_timer_get_time() / 1000) - transfer_timeout_ms;
    echo("Downloaded %dB in %dms (%d chunks)", total_bytes_received, (int32_t)(transfer_end_ms - transfer_start_ms), chunk_count);

    echo("Waiting for flash operations to complete...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    result = esp_ota_end(ota_handle);
    if (result == ESP_OK) {
        result = esp_ota_set_boot_partition(ota_partition);
        if (result == ESP_OK) {
            echo("OTA OK, restarting...");
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

bool run_uart_bridge_for_device_ota(uart_port_t upstream_port, uart_port_t downstream_port) {
    constexpr int64_t IDLE_TIMEOUT_MS = 10000;

    echo("Starting device OTA bridge (upstream: %d, downstream: %d)", upstream_port, downstream_port);
    // No longer send core.ota() to downstream device here

    // flush any old data from the buffers
    uart_flush_input(upstream_port);
    uart_flush_input(downstream_port);

    unsigned long last_data = millis();
    uint32_t loop_count = 0;

    while (true) {
        if (ota_bridge_should_stop) {
            echo("Bridge stop requested");
            break;
        }

        bool data_transferred = false;

        size_t avail = 0;
        uart_get_buffered_data_len(upstream_port, &avail);
        if (avail) {
            uint8_t buf[1024];
            int n = uart_read_bytes(upstream_port, buf, std::min<size_t>(avail, sizeof buf), 0);
            if (n > 0) {
                uart_write_bytes(downstream_port, buf, n);
                last_data = millis();
                data_transferred = true;
            }
        }

        uart_get_buffered_data_len(downstream_port, &avail);
        if (avail) {
            uint8_t buf[1024];
            int n = uart_read_bytes(downstream_port, buf, std::min<size_t>(avail, sizeof buf), 0);
            if (n > 0) {
                uart_write_bytes(upstream_port, buf, n);
                last_data = millis();
                data_transferred = true;
            }
        }

        if (millis_since(last_data) > IDLE_TIMEOUT_MS) {
            echo("last_data: %d. IDLE_TIMEOUT_MS: %d. millis_since: %d", last_data, IDLE_TIMEOUT_MS, millis_since(last_data));
            echo("Bridge idle – leaving bridge mode");
            break;
        }

        taskYIELD();

        loop_count++;
        if (!data_transferred || (loop_count % 100 == 0)) {
            vTaskDelay(1);
            esp_task_wdt_reset();
        }
    }

    return true;
}

static void ota_bridge_task_wrapper(void *parameter) {
    esp_task_wdt_add(NULL);
    run_uart_bridge_for_device_ota(bridge_upstream_port, bridge_downstream_port);
    echo("OTA bridge task completed");
    esp_task_wdt_delete(NULL);

    ota_bridge_task_handle = nullptr;
    vTaskDelete(NULL);
}

void start_ota_bridge_task(uart_port_t upstream_port, uart_port_t downstream_port) {
    bridge_upstream_port = upstream_port;
    bridge_downstream_port = downstream_port;
    ota_bridge_should_stop = false;

    BaseType_t result = xTaskCreatePinnedToCore(
        ota_bridge_task_wrapper,
        "ota_bridge",
        8192,
        NULL,
        5,
        &ota_bridge_task_handle,
        1);

    if (result != pdPASS) {
        echo("Failed to create OTA bridge task");
        ota_bridge_task_handle = nullptr;
    } else {
        echo("OTA bridge task created on core 1 (upstream: %d, downstream: %d)", upstream_port, downstream_port);
    }
}

bool is_uart_bridge_running() {
    return ota_bridge_task_handle != nullptr;
}

std::vector<std::string> build_bridge_path(const std::string &target_name) {
    std::vector<std::string> bridge_path;
    std::string current_target = target_name;

    echo("Building bridge path for target: %s", target_name.c_str());

    // Keep following the parent chain until we reach core
    while (current_target != "core") {
        try {
            Module_ptr module = Global::get_module(current_target);

            if (module->type == proxy) {
                // It's a proxy, get its parent
                Proxy *proxy_module = static_cast<Proxy *>(module.get());
                std::string parent = proxy_module->get_parent_expander_name();

                echo("Found proxy %s -> parent: %s", current_target.c_str(), parent.c_str());

                // Add parent to bridge path
                bridge_path.push_back(parent);
                current_target = parent;
            } else if (module->type == expander || module->type == plexus_expander) {
                // It's an expander/plexus_expander, it must be on core
                echo("Found expander %s -> must be on core", current_target.c_str());

                // Add core to bridge path
                bridge_path.push_back("core");
                break;
            } else {
                // Unknown module type, assume it's on core
                echo("Unknown module type %s, assuming on core", current_target.c_str());
                break;
            }
        } catch (const std::runtime_error &e) {
            echo("Error accessing module %s: %s", current_target.c_str(), e.what());
            return {}; // Return empty vector on error
        }
    }

    echo("Bridge path built: %d bridges needed", (int)bridge_path.size());
    for (const auto &bridge : bridge_path) {
        echo("  Bridge: %s", bridge.c_str());
    }

    return bridge_path;
}

bool activate_bridges(const std::vector<std::string> &bridge_path) {
    echo("Activating %d bridges", (int)bridge_path.size());

    for (auto it = bridge_path.rbegin(); it != bridge_path.rend(); ++it) {
        const std::string &bridge_name = *it;
        try {
            Module_ptr bridge_module = Global::get_module(bridge_name);

            echo("Activating bridge on %s", bridge_name.c_str());

            std::vector<ConstExpression_ptr> empty_args;
            bridge_module->call("ota_bridge_start", empty_args);

            vTaskDelay(100 / portTICK_PERIOD_MS);

        } catch (const std::runtime_error &e) {
            echo("Failed to activate bridge %s: %s", bridge_name.c_str(), e.what());
            return false;
        }
    }

    echo("All bridges activated successfully");
    return true;
}

std::vector<std::string> detect_required_bridges(const std::string &target_name) {
    if (target_name == "core") {
        echo("Target is core, no bridges needed");
        return {};
    }

    // Check if target module exists
    try {
        Module_ptr target_module = Global::get_module(target_name);
        echo("Target module %s found, type: %d", target_name.c_str(), target_module->type);
    } catch (const std::runtime_error &e) {
        echo("Target module %s not found: %s", target_name.c_str(), e.what());
        return {}; // Return empty vector if target doesn't exist
    }

    return build_bridge_path(target_name);
}

bool perform_automatic_ota(const std::string &target_name) {
    echo("Starting automatic OTA for target: %s", target_name.c_str());

    // Detect required bridges
    std::vector<std::string> bridge_path = detect_required_bridges(target_name);

    // Activate bridges if needed
    if (!bridge_path.empty()) {
        if (!activate_bridges(bridge_path)) {
            echo("Bridge activation failed, aborting OTA");
            return false;
        }
    }

    // Proceed with OTA
    echo("Bridges ready, starting OTA process");
    return receive_firmware_via_uart();
}

} // namespace ota
