#include "bus_backup.h"

#include "../global.h"
#include "../modules/serial.h"
#include "../modules/serial_bus.h"
#include "uart.h"

#include "nvs.h"

#define NVS_NAMESPACE "bus_backup"

namespace bus_backup {

void save_if_present() {
    for (const auto &[name, module] : Global::modules) {
        if (module->type != serial_bus) {
            continue;
        }
        const auto bus = std::static_pointer_cast<SerialBus>(module);
        nvs_handle handle;
        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
            return;
        }
        nvs_set_i8(handle, "tx", bus->serial->tx_pin);
        nvs_set_i8(handle, "rx", bus->serial->rx_pin);
        nvs_set_i32(handle, "baud", bus->serial->baud_rate);
        nvs_set_i8(handle, "uart", bus->serial->uart_num);
        nvs_set_i8(handle, "node", bus->node_id);
        nvs_commit(handle);
        nvs_close(handle);
        return;
    }
}

void restore_if_needed() {
    for (const auto &[name, module] : Global::modules) {
        if (module->type == serial_bus) {
            return;
        }
    }

    nvs_handle handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
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

    echo("restoring serial bus from backup");
    try {
        std::vector<std::string> serials_to_remove;
        for (const auto &[name, module] : Global::modules) {
            if (module->type == serial) {
                serials_to_remove.push_back(name);
            }
        }
        for (const std::string &name : serials_to_remove) {
            Global::modules.erase(name);
            Global::variables.erase(name);
        }
        Serial_ptr backup_serial = std::make_shared<Serial>(
            "_backup_serial", static_cast<gpio_num_t>(rx), static_cast<gpio_num_t>(tx),
            baud, static_cast<uart_port_t>(uart));
        Global::add_module("_backup_serial", backup_serial);
        const auto bus = std::make_shared<SerialBus>("_backup_bus", backup_serial, node);
        Global::add_module("_backup_bus", bus);
    } catch (const std::runtime_error &e) {
        echo("bus backup error: %s", e.what());
    }
}

void remove() {
    nvs_handle handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
}

} // namespace bus_backup
