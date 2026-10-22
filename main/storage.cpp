#include "storage.h"
#include "esp_check.h"
#include "nvs.h"
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
static constexpr size_t CHUNK_SIZE = 959;

bool Storage::editing = false;
std::string Storage::save_error;
std::vector<std::string> Storage::edit_pieces;

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

void write(const std::string &ns, const std::string &key, const std::string &value) {
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
    if (!Storage::save_error.empty()) {
        throw std::runtime_error("the stored startup script is incomplete: " + Storage::save_error);
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
    Storage::save_error.clear(); // the edit changed: its checksum is reported again
}

void Storage::remove_from_startup(const std::string &prefix) {
    if (prefix.empty()) {
        // the usual start of a rewrite: every line goes, so the old script is not even read
        std::vector<std::string>().swap(Storage::edit_pieces);
        Storage::editing = true;
        Storage::save_error.clear(); // the edit changed: its checksum is reported again
        return;
    }
    Storage::begin_edit(true);
    std::vector<std::string> old_pieces;
    old_pieces.swap(Storage::edit_pieces);
    bool changed = false;
    const std::function<void(const std::string &line)> keep = [&prefix, &changed](const std::string &line) {
        if (starts_with(line, prefix)) {
            changed = true;
        } else {
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
    if (changed) {
        Storage::save_error.clear(); // the edit changed: its checksum is reported again
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
        // the old chunks are erased already, so NVS holds an incomplete script; the edit stays in RAM for another `!.`
        Storage::save_error = e.what();
        throw std::runtime_error(std::string(e.what()) + "; the stored startup script is incomplete");
    } catch (const std::exception &e) {
        Storage::save_error = e.what();
        throw;
    }
    std::vector<std::string>().swap(Storage::edit_pieces);
    Storage::editing = false;
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
