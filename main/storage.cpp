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

// 959 characters and the terminator fill 30 NVS entries plus one header entry, so four chunks share a page of 126
// entries and still fit on fragmented pages, where a chunk of 0xf00 needs an almost empty one
static constexpr size_t CHUNK_SIZE = 959;

bool Storage::editing = false;
bool Storage::save_failed = false;
std::vector<std::string> Storage::edit_pieces;

void Storage::init() {
    nvs_flash_init();
}

void write(const std::string &ns, const std::string &key, const std::string &value) {
    esp_err_t err;
    nvs_handle handle;
    if ((err = nvs_open(ns.c_str(), NVS_READWRITE, &handle)) != ESP_OK) {
        throw std::runtime_error("could not open storage namespace \"" + ns + "\" (" + std::string(esp_err_to_name(err)) + ")");
    }
    // the value only starts the message: a whole chunk would make it too long to be printed at all
    std::string shown = value.size() > 40 ? value.substr(0, 40) + "..." : value;
    std::replace(shown.begin(), shown.end(), '\n', ' ');
    if ((err = nvs_set_str(handle, key.c_str(), value.c_str())) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not write to storage " + ns + "." + key + "=" + shown + " (" + std::string(esp_err_to_name(err)) + ")");
    }
    if ((err = nvs_commit(handle)) != ESP_OK) {
        nvs_close(handle);
        throw std::runtime_error("could not commit to storage " + ns + "." + key + "=" + shown + " (" + std::string(esp_err_to_name(err)) + ")");
    }
    nvs_close(handle);
}

// false only if the namespace or the key does not exist; any other error is left for read() to report
static bool exists(const std::string &ns, const std::string &key) {
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

std::string read(const std::string &ns, const std::string &key) {
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

void Storage::read_startup(const std::function<void(const std::string &piece)> &piece) {
    if (Storage::editing) {
        for (const std::string &edit_piece : Storage::edit_pieces) {
            piece(edit_piece);
        }
        return;
    }
    Storage::read_stored(piece);
}

void Storage::read_stored(const std::function<void(const std::string &piece)> &piece) {
    if (!exists(NAMESPACE, "num_chunks")) {
        return; // fresh or erased NVS: no startup script yet
    }
    const int num_chunks = std::stoi(read(NAMESPACE, "num_chunks"));
    for (int i = 0; i < num_chunks; i++) {
        piece(read(NAMESPACE, "chunk" + std::to_string(i)));
    }
}

// Collects pieces of text and calls `line` for every complete line (without its '\n'); `finish` delivers a last line
// that lacks one. A line may span pieces.
class LineSplitter {
private:
    const std::function<void(const std::string &line)> &line;
    std::string carry;

public:
    explicit LineSplitter(const std::function<void(const std::string &line)> &line) : line(line) {
    }

    void feed(const std::string &piece) {
        size_t start = 0;
        size_t end;
        while ((end = piece.find('\n', start)) != std::string::npos) {
            if (carry.empty()) {
                line(piece.substr(start, end - start));
            } else {
                carry.append(piece, start, end - start);
                line(carry);
                carry.clear();
            }
            start = end + 1;
        }
        carry.append(piece, start, std::string::npos);
    }

    void finish() {
        if (!carry.empty()) {
            line(carry);
            carry.clear();
        }
    }
};

void Storage::read_startup_lines(const std::function<void(const std::string &line)> &line) {
    LineSplitter splitter(line);
    Storage::read_startup([&splitter](const std::string &piece) { splitter.feed(piece); });
    splitter.finish();
}

std::uint16_t Storage::startup_checksum() {
    // of the edit while there is one (a host may check it before `!.`), else of the stored script; a read error or a
    // failed `!.` gives no checksum at all
    if (Storage::save_failed) {
        throw std::runtime_error("the startup script could not be saved, the stored one is incomplete");
    }
    std::uint16_t checksum = 0;
    Storage::read_startup([&checksum](const std::string &piece) {
        for (const char c : piece) {
            checksum += static_cast<std::uint8_t>(c);
        }
    });
    return checksum;
}

void Storage::begin_edit(const bool keep_current) {
    if (Storage::editing) {
        return;
    }
    std::vector<std::string> pieces;
    if (keep_current) {
        // a read error ends the command: an empty edit would replace the stored script at `!.`; `!-` needs no read
        Storage::read_stored([&pieces](const std::string &piece) { pieces.push_back(piece); });
    }
    Storage::edit_pieces.swap(pieces);
    Storage::editing = true;
}

void Storage::append_to_edit(const char *text, size_t length) {
    while (length > 0) {
        if (Storage::edit_pieces.empty() || Storage::edit_pieces.back().size() >= CHUNK_SIZE) {
            Storage::edit_pieces.emplace_back();
        }
        std::string &last = Storage::edit_pieces.back();
        if (last.capacity() < CHUNK_SIZE) {
            // exactly one chunk of room; growing the string itself would double its capacity
            std::string roomy;
            roomy.reserve(CHUNK_SIZE);
            roomy.append(last);
            last.swap(roomy);
        }
        const size_t take = std::min(length, CHUNK_SIZE - last.size());
        last.append(text, take);
        text += take;
        length -= take;
    }
}

void Storage::append_to_startup(const std::string &line) {
    Storage::begin_edit(true);
    Storage::save_failed = false; // the edit changes: the checksum covers it again
    // all or nothing: a line that does not fit into the heap leaves the script as it was
    const size_t pieces = Storage::edit_pieces.size();
    const size_t last_size = pieces ? Storage::edit_pieces.back().size() : 0;
    try {
        Storage::append_to_edit(line.data(), line.size());
        Storage::append_to_edit("\n", 1);
    } catch (...) {
        Storage::edit_pieces.resize(pieces);
        if (pieces) {
            Storage::edit_pieces.back().resize(last_size);
        }
        throw;
    }
}

void Storage::remove_from_startup(const std::string &prefix) {
    if (prefix.empty()) {
        // the usual start of a rewrite: every line goes, so the old script is not even read
        std::vector<std::string>().swap(Storage::edit_pieces);
        Storage::editing = true;
        Storage::save_failed = false;
        return;
    }
    Storage::begin_edit(true);
    Storage::save_failed = false;
    std::vector<std::string> old_pieces;
    old_pieces.swap(Storage::edit_pieces);
    const std::function<void(const std::string &line)> keep = [&prefix](const std::string &line) {
        if (!starts_with(line, prefix)) {
            Storage::append_to_edit(line.data(), line.size());
            Storage::append_to_edit("\n", 1);
        }
    };
    try {
        LineSplitter splitter(keep);
        for (const std::string &piece : old_pieces) {
            splitter.feed(piece);
        }
        splitter.finish();
    } catch (...) {
        Storage::edit_pieces.swap(old_pieces); // all or nothing, as with `!+`
        throw;
    }
}

void Storage::print_startup(const std::string &prefix) {
    try {
        Storage::read_startup_lines([&prefix](const std::string &line) {
            if (starts_with(line, prefix)) {
                echo("%s", line.c_str());
            }
        });
    } catch (const std::runtime_error &e) {
        echo("warning: %s", e.what());
    }
}

void Storage::save_startup() {
    if (!Storage::editing) {
        return; // NVS already holds the script
    }
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
    size_t num_chunks = 0;
    for (const std::string &piece : Storage::edit_pieces) {
        num_chunks += (piece.size() + CHUNK_SIZE - 1) / CHUNK_SIZE; // pieces read from larger chunks of older firmware
    }
    try {
        write(NAMESPACE, "num_chunks", std::to_string(num_chunks));
        size_t chunk = 0;
        for (const std::string &piece : Storage::edit_pieces) {
            if (piece.size() <= CHUNK_SIZE) {
                write(NAMESPACE, "chunk" + std::to_string(chunk++), piece);
                continue;
            }
            for (size_t pos = 0; pos < piece.size(); pos += CHUNK_SIZE) {
                write(NAMESPACE, "chunk" + std::to_string(chunk++), piece.substr(pos, CHUNK_SIZE));
            }
        }
    } catch (const std::runtime_error &e) {
        // the edit stays in RAM, so that another `!.` can store it
        Storage::save_failed = true;
        throw std::runtime_error(std::string(e.what()) + "; the stored startup script is incomplete");
    } catch (...) {
        Storage::save_failed = true;
        throw;
    }
    std::vector<std::string>().swap(Storage::edit_pieces);
    Storage::editing = false;
    Storage::save_failed = false;
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
