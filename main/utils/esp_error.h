#pragma once

#include <esp_err.h>
#include <stdexcept>
#include <string>

// Throws a runtime_error that names the IDF error when a driver call did not return ESP_OK.
inline void check_error(const esp_err_t err, const std::string &msg) {
    if (err != ESP_OK) {
        throw std::runtime_error(msg + ": " + esp_err_to_name(err));
    }
}
