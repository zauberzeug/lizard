#include "storage.h"
#include "nvs_flash.h"
#include "utils/string_utils.h"
#include "utils/uart.h"
#include <stdexcept>
#include <string>

#define NAMESPACE "storage"
#define MAX_CHUNK_SIZE 0xf80

std::string Storage::startup;

void Storage::init() {
    nvs_flash_init();
    Storage::startup = Storage::get();
}

void write(const std::string ns, const std::string key, const std::string value) {
    nvs_handle handle;
    if (nvs_open(ns.c_str(), NVS_READWRITE, &handle) != ESP_OK) {
        throw std::runtime_error("could not open storage namespace \"" + ns + "\"");
    }
    if (nvs_set_str(handle, key.c_str(), value.c_str()) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not write to storage " + ns + "." + key + "=" + value);
    }
    if (nvs_commit(handle) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not commit to storage " + ns + "." + key + "=" + value);
    }
    nvs_close(handle);
}

std::string read(const std::string ns, const std::string key) {
    nvs_handle handle;
    if (nvs_open(ns.c_str(), NVS_READWRITE, &handle) != ESP_OK) {
        throw std::runtime_error("could not open storage namespace \"" + ns + "\"");
    }
    size_t size = 0;
    if (nvs_get_str(handle, key.c_str(), NULL, &size) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not peek storage " + ns + "." + key);
    }
    char *value = (char *)malloc(size);
    if (size > 0) {
        if (nvs_get_str(handle, key.c_str(), value, &size) != ESP_OK) {
            free(value);
            nvs_close(handle);
            throw std::runtime_error("could not read storage " + ns + "." + key);
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
    const int num_chunks = std::stoi(read(NAMESPACE, "num_chunks"));
    for (int i = 0; i < num_chunks; i++) {
        result += read(NAMESPACE, "chunk" + std::to_string(i));
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
