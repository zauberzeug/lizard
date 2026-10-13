#include "storage.h"
#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "utils/string_utils.h"
#include "utils/uart.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>

#define NAMESPACE "storage"
// NVS stores a string as one header entry plus one 32-byte entry per 32 bytes of text and terminator, all on the same
// page of 126 entries. A chunk of 959 bytes takes 31 entries, so four chunks fill a page, and a single chunk still finds
// room on a page that holds other keys. The former 0xf00 bytes took 122 entries and needed an almost empty page.
// Reading does not depend on this size: a script stored in larger chunks still loads.
#define MAX_CHUNK_SIZE 959

std::string Storage::startup;
std::string Storage::save_error;

void Storage::init() {
    nvs_flash_init();
    Storage::startup = Storage::get();
}

// the start of a value for an error message: a message with a whole chunk would exceed the console line and be dropped
static std::string preview(const std::string &value) {
    size_t length = std::min(value.size(), size_t(40));
    while (length < value.size() && (static_cast<unsigned char>(value[length]) & 0xC0) == 0x80) {
        length--; // do not cut a multi-byte UTF-8 character
    }
    std::string shown = length < value.size() ? value.substr(0, length) + "..." : value;
    std::replace(shown.begin(), shown.end(), '\n', ' ');
    return shown;
}

void write(const std::string ns, const std::string key, const std::string value) {
    esp_err_t err;
    nvs_handle handle;
    if ((err = nvs_open(ns.c_str(), NVS_READWRITE, &handle)) != ESP_OK) {
        throw std::runtime_error("could not open storage namespace \"" + ns + "\" (" + std::string(esp_err_to_name(err)) + ")");
    }
    if ((err = nvs_set_str(handle, key.c_str(), value.c_str())) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not write to storage " + ns + "." + key + "=" + preview(value) + " (" + std::string(esp_err_to_name(err)) + ")");
    }
    if ((err = nvs_commit(handle)) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not commit to storage " + ns + "." + key + "=" + preview(value) + " (" + std::string(esp_err_to_name(err)) + ")");
    }
    nvs_close(handle);
}

// false only if the namespace or the key does not exist; any other error is left for read() to report
static bool exists(const std::string ns, const std::string key) {
    nvs_handle handle;
    esp_err_t err = nvs_open(ns.c_str(), NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }
    if (err != ESP_OK) {
        return true;
    }
    size_t size = 0;
    err = nvs_get_str(handle, key.c_str(), NULL, &size);
    nvs_close(handle);
    return err != ESP_ERR_NVS_NOT_FOUND;
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
    if (value == NULL) {
        nvs_close(handle);
        throw std::runtime_error("could not allocate " + std::to_string(size) + " bytes for storage " + ns + "." + key);
    }
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

void write_u32(const std::string ns, const std::string key, const std::uint32_t value) {
    esp_err_t err;
    nvs_handle handle;
    if ((err = nvs_open(ns.c_str(), NVS_READWRITE, &handle)) != ESP_OK) {
        throw std::runtime_error("could not open storage namespace \"" + ns + "\" (" + std::string(esp_err_to_name(err)) + ")");
    }
    if ((err = nvs_set_u32(handle, key.c_str(), value)) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not write to storage " + ns + "." + key + " (" + std::string(esp_err_to_name(err)) + ")");
    }
    if ((err = nvs_commit(handle)) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not commit to storage " + ns + "." + key + " (" + std::string(esp_err_to_name(err)) + ")");
    }
    nvs_close(handle);
}

bool read_u32(const std::string ns, const std::string key, std::uint32_t &out) {
    esp_err_t err;
    nvs_handle handle;
    if ((err = nvs_open(ns.c_str(), NVS_READWRITE, &handle)) != ESP_OK) {
        return false;
    }
    uint32_t value = 0;
    err = nvs_get_u32(handle, key.c_str(), &value);
    nvs_close(handle);
    if (err == ESP_OK) {
        out = value;
        return true;
    }
    return false;
}

void Storage::put(const std::string value) {
    if (exists(NAMESPACE, "num_chunks")) {
        try {
            const int old_num_chunks = std::stoi(read(NAMESPACE, "num_chunks"));
            for (int i = 0; i < old_num_chunks; i++) {
                nvs_delete_key(NAMESPACE, "chunk" + std::to_string(i));
            }
        } catch (...) {
            echo("warning: could not delete old chunks before writing new ones");
        }
    }

    write(NAMESPACE, "num_chunks", std::to_string((value.length() + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE));
    for (size_t pos = 0; pos < value.length(); pos += MAX_CHUNK_SIZE) {
        write(NAMESPACE, "chunk" + std::to_string(pos / MAX_CHUNK_SIZE), value.substr(pos, MAX_CHUNK_SIZE));
    }
}

std::string Storage::get() {
    std::string result = "";
    if (!exists(NAMESPACE, "num_chunks")) {
        return result; // fresh or erased NVS: no startup script yet
    }
    const int num_chunks = std::stoi(read(NAMESPACE, "num_chunks"));
    for (int i = 0; i < num_chunks; i++) {
        result += read(NAMESPACE, "chunk" + std::to_string(i));
    }
    return result;
}

void Storage::append_to_startup(const std::string line) {
    Storage::startup += line + '\n';
    Storage::save_error.clear(); // the script changed: its checksum is reported again
}

void Storage::remove_from_startup(const std::string substring) {
    std::string new_startup = "";
    bool changed = false;
    while (!Storage::startup.empty()) {
        std::string line = cut_first_word(Storage::startup, '\n');
        if (starts_with(line, substring)) {
            changed = true;
        } else {
            new_startup += line + '\n';
        }
    }
    Storage::startup = new_startup;
    if (changed) {
        Storage::save_error.clear(); // the script changed: its checksum is reported again
    }
}

void Storage::print_startup(const std::string substring) {
    std::string startup = Storage::startup;
    while (!startup.empty()) {
        std::string line = cut_first_word(startup, '\n');
        if (starts_with(line, substring)) {
            echo("%s", line.c_str());
        }
    }
}

void Storage::save_startup() {
    try {
        Storage::put(Storage::startup);
    } catch (const std::exception &e) {
        Storage::save_error = e.what(); // put() erases the stored script before writing the new one, so NVS holds an incomplete one
        throw;
    }
    Storage::save_error.clear();
}

void Storage::set_user_pin(const std::uint32_t pin) {
    write_u32("ble_pins", "user_pin", pin);
}

bool Storage::get_user_pin(std::uint32_t &pin) {
    return read_u32("ble_pins", "user_pin", pin);
}

void Storage::nvs_delete_key(const std::string &ns, const std::string &key) {
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

void Storage::set_baudrate(const std::uint32_t baudrate) {
    write_u32("uart", "baudrate", baudrate);
}

bool Storage::get_baudrate(std::uint32_t &baudrate) {
    return read_u32("uart", "baudrate", baudrate);
}
