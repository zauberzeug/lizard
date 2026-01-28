#include "bus_backup.h"

#include "../global.h"
#include "../modules/serial.h"
#include "../modules/serial_bus.h"
#include "uart.h"

#include "nvs.h"

#define NAMESPACE "bus_backup"

namespace bus_backup {

void restore_if_needed() {
    // Already have a serial bus? Nothing to restore.
    for (const auto &[name, module] : Global::modules) {
        if (module->type == serial_bus) {
            return;
        }
    }

    // Load backup config from NVS
    nvs_handle handle;
    if (nvs_open(NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    int8_t tx, rx, uart, node;
    int32_t baud;
    bool ok = nvs_get_i8(handle, "tx", &tx) == ESP_OK &&
              nvs_get_i8(handle, "rx", &rx) == ESP_OK &&
              nvs_get_i32(handle, "baud", &baud) == ESP_OK &&
              nvs_get_i8(handle, "uart", &uart) == ESP_OK &&
              nvs_get_i8(handle, "node", &node) == ESP_OK;
    nvs_close(handle);
    if (!ok) {
        return;
    }

    // Find existing serial with matching pins, and track which UARTs are in use
    Serial_ptr matching_serial;
    bool used_uarts[3] = {true, false, false}; // UART0 reserved for console
    for (const auto &[name, module] : Global::modules) {
        const auto serial = std::dynamic_pointer_cast<Serial>(module);
        if (!serial) {
            continue;
        }
        if (serial->tx_pin == tx && serial->rx_pin == rx) {
            matching_serial = serial;
        }
        if (serial->uart_num < 3) {
            used_uarts[serial->uart_num] = true;
        }
    }

    // Create serial bus using existing serial or a new one
    echo("no serial bus found, restoring from backup");
    try {
        Serial_ptr backup_serial;
        if (matching_serial) {
            backup_serial = matching_serial;
        } else {
            int free_uart = -1;
            if (!used_uarts[uart])
                free_uart = uart;
            else if (!used_uarts[1])
                free_uart = 1;
            else if (!used_uarts[2])
                free_uart = 2;
            if (free_uart < 0) {
                echo("bus backup: no free uart available");
                return;
            }
            backup_serial = std::make_shared<Serial>(
                "_backup_serial", static_cast<gpio_num_t>(rx), static_cast<gpio_num_t>(tx),
                baud, static_cast<uart_port_t>(free_uart));
            Global::add_module("_backup_serial", backup_serial);
        }
        const auto bus = std::make_shared<SerialBus>("_backup_bus", backup_serial, node);
        Global::add_module("_backup_bus", bus);
    } catch (const std::runtime_error &e) {
        echo("error restoring bus backup: %s", e.what());
    }
}

void save(const int tx_pin, const int rx_pin, const long baud_rate, const int uart_num, const int node_id) {
    nvs_handle handle;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_i8(handle, "tx", tx_pin);
    nvs_set_i8(handle, "rx", rx_pin);
    nvs_set_i32(handle, "baud", baud_rate);
    nvs_set_i8(handle, "uart", uart_num);
    nvs_set_i8(handle, "node", node_id);
    nvs_commit(handle);
    nvs_close(handle);
}

void remove() {
    nvs_handle handle;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
}

} // namespace bus_backup
