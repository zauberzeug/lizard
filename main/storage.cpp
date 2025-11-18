#include "storage.h"
#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "utils/addressing.h"
#include "utils/string_utils.h"
#include "utils/uart.h"
#include <cstdint>
#include <stdexcept>
#include <string>

#define NAMESPACE "storage"
#define MAX_CHUNK_SIZE 0xf00

std::string Storage::startup;

void Storage::init() {
    ESP_ERROR_CHECK(nvs_flash_init());
    Storage::startup = Storage::get();
}

void write(const std::string ns, const std::string key, const std::string value) {
    esp_err_t err;
    nvs_handle handle;
    if ((err = nvs_open(ns.c_str(), NVS_READWRITE, &handle)) != ESP_OK) {
        throw std::runtime_error("could not open storage namespace \"" + ns + "\" (" + std::string(esp_err_to_name(err)) + ")");
    }
    if ((err = nvs_set_str(handle, key.c_str(), value.c_str())) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not write to storage " + ns + "." + key + "=" + value + " (" + std::string(esp_err_to_name(err)) + ")");
    }
    if ((err = nvs_commit(handle)) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not commit to storage " + ns + "." + key + "=" + value + " (" + std::string(esp_err_to_name(err)) + ")");
    }
    nvs_close(handle);
}

std::string read(const std::string ns, const std::string key) {
    esp_err_t err;
    nvs_handle handle;
    if ((err = nvs_open(ns.c_str(), NVS_READWRITE, &handle)) != ESP_OK) {
        throw std::runtime_error("could not open storage namespace \"" + ns + "\" (" + std::string(esp_err_to_name(err)) + ")");
    }
    size_t size = 0;
    if ((err = nvs_get_str(handle, key.c_str(), NULL, &size)) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not peek storage " + ns + "." + key + " (" + std::string(esp_err_to_name(err)) + ")");
    }
    char *value = (char *)malloc(size);
    if (size > 0) {
        if ((err = nvs_get_str(handle, key.c_str(), value, &size)) != ESP_OK) {
            free(value);
            nvs_close(handle);
            throw std::runtime_error("could not read storage " + ns + "." + key + " (" + std::string(esp_err_to_name(err)) + ")");
        }
    }
    std::string result = std::string(value);
    free(value);
    nvs_close(handle);
    return result;
}

void Storage::put(const std::string value) {
    int num_chunks = 0;
    for (int pos = 0; pos < value.length(); pos += MAX_CHUNK_SIZE) {
        num_chunks++;
        write(NAMESPACE, "chunk" + std::to_string(pos / MAX_CHUNK_SIZE), value.substr(pos, MAX_CHUNK_SIZE));
    }
    write(NAMESPACE, "num_chunks", std::to_string(num_chunks));
}

std::string Storage::get() {
    std::string result = "";
    try {
        const int num_chunks = std::stoi(read(NAMESPACE, "num_chunks"));
        for (int i = 0; i < num_chunks; i++) {
            result += read(NAMESPACE, "chunk" + std::to_string(i));
        }
    } catch (const std::runtime_error &e) {
        // NVS is empty or corrupted, return empty string
        result = "";
    }
    return result;
}

void Storage::append_to_startup(const std::string line) {
    Storage::startup += line + '\n';
}

void Storage::remove_from_startup(const std::string substring) {
    std::string new_startup = "";
    while (!Storage::startup.empty()) {
        std::string line = cut_first_word(Storage::startup, '\n');
        if (!starts_with(line, substring)) {
            new_startup += line + '\n';
        }
    }
    Storage::startup = new_startup;
}

void Storage::print_startup(const std::string substring) {
    std::string startup = Storage::startup;
    while (!startup.empty()) {
        std::string line = cut_first_word(startup, '\n');
        if (starts_with(line, substring)) {
            echo(line.c_str());
        }
    }
}

void Storage::save_startup() {
    Storage::put(Storage::startup);
}

void Storage::clear_nvs() {
    Storage::put("");
}

void Storage::set_user_pin(const std::uint32_t pin) {
    esp_err_t err;
    nvs_handle handle;
    if ((err = nvs_open("ble_pins", NVS_READWRITE, &handle)) != ESP_OK) {
        throw std::runtime_error("could not open storage namespace \"ble_pins\" (" + std::string(esp_err_to_name(err)) + ")");
    }
    if ((err = nvs_set_u32(handle, "user_pin", pin)) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not write user_pin (" + std::string(esp_err_to_name(err)) + ")");
    }
    if ((err = nvs_commit(handle)) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not commit user_pin (" + std::string(esp_err_to_name(err)) + ")");
    }
    nvs_close(handle);
}

bool Storage::get_user_pin(std::uint32_t &pin) {
    esp_err_t err;
    nvs_handle handle;
    if ((err = nvs_open("ble_pins", NVS_READWRITE, &handle)) != ESP_OK) {
        return false;
    }
    uint32_t value = 0;
    err = nvs_get_u32(handle, "user_pin", &value);
    nvs_close(handle);
    if (err == ESP_OK) {
        pin = value;
        return true;
    }
    return false;
}

static void nvs_delete_key(const std::string &ns, const std::string &key) {
    esp_err_t err;
    nvs_handle handle;
    if ((err = nvs_open(ns.c_str(), NVS_READWRITE, &handle)) != ESP_OK) {
        throw std::runtime_error("could not open storage namespace \"" + ns + "\" (" + std::string(esp_err_to_name(err)) + ")");
    }
    err = nvs_erase_key(handle, key.c_str());
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        throw std::runtime_error("could not erase key " + ns + "." + key + " (" + std::string(esp_err_to_name(err)) + ")");
    }
    if ((err = nvs_commit(handle)) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not commit erase for key " + ns + "." + key + " (" + std::string(esp_err_to_name(err)) + ")");
    }
    nvs_close(handle);
}

void Storage::remove_user_pin() {
    nvs_delete_key("ble_pins", "user_pin");
}

void Storage::put_device_id(const uint8_t id) {
    esp_err_t err;
    nvs_handle handle;
    if ((err = nvs_open(NAMESPACE, NVS_READWRITE, &handle)) != ESP_OK) {
        throw std::runtime_error("could not open storage namespace for device ID");
    }

    if ((err = nvs_set_u8(handle, "device_id", id)) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not write device ID to storage");
    }

    if ((err = nvs_commit(handle)) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not commit device ID to storage");
    }
    nvs_close(handle);
}

void Storage::load_device_id() {
    esp_err_t err;
    nvs_handle handle;
    if ((err = nvs_open(NAMESPACE, NVS_READWRITE, &handle)) != ESP_OK) {
        throw std::runtime_error("could not open storage namespace for device ID");
    }

    uint8_t value;
    if ((err = nvs_get_u8(handle, "device_id", &value)) != ESP_OK) {
        nvs_close(handle);
        // Device ID not found in storage, use default (no error)
        return;
    }
    // Convert numeric ID (0-9) to character for UART system ('0'-'9')
    set_uart_expander_id(static_cast<char>('0' + value));
    nvs_close(handle);
}

void Storage::put_external_mode(const bool enabled) {
    esp_err_t err;
    nvs_handle handle;
    if ((err = nvs_open(NAMESPACE, NVS_READWRITE, &handle)) != ESP_OK) {
        throw std::runtime_error("could not open storage namespace for external mode");
    }

    if ((err = nvs_set_u8(handle, "external_mode", enabled ? 1 : 0)) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not write external mode to storage");
    }

    if ((err = nvs_commit(handle)) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not commit external mode to storage");
    }
    nvs_close(handle);
}

void Storage::load_external_mode() {
    esp_err_t err;
    nvs_handle handle;
    if ((err = nvs_open(NAMESPACE, NVS_READWRITE, &handle)) != ESP_OK) {
        throw std::runtime_error("could not open storage namespace for external mode");
    }

    uint8_t value;
    if ((err = nvs_get_u8(handle, "external_mode", &value)) != ESP_OK) {
        nvs_close(handle);
        // External mode not found in storage, use default (no error)
        return;
    }

    // Restore the external mode state
    if (value) {
        activate_uart_external_mode();
    } else {
        deactivate_uart_external_mode();
    }
    nvs_close(handle);
}
