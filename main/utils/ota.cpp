#include "ota.h"
#include "timing.h"
#include "uart.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace ota {

static bool starts_with(std::string_view text, const char *prefix) {
    const size_t prefix_len = std::strlen(prefix);
    return text.size() >= prefix_len && text.compare(0, prefix_len, prefix) == 0;
}

static bool is_decimal(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

static bool decode_base64_chunk(std::string_view input, std::array<uint8_t, BUS_OTA_BUFFER_SIZE> &output, size_t &out_len) {
    auto decode_char = [](char c) -> int {
        return (c >= 'A' && c <= 'Z')   ? c - 'A'
               : (c >= 'a' && c <= 'z') ? c - 'a' + 26
               : (c >= '0' && c <= '9') ? c - '0' + 52
               : (c == '+')             ? 62
               : (c == '/')             ? 63
               : (c == '=')             ? -1
                                        : -2;
    };

    out_len = 0;
    if (input.empty() || (input.size() % 4) != 0) {
        return false;
    }

    for (size_t i = 0; i < input.size(); i += 4) {
        const int c0 = decode_char(input[i]);
        const int c1 = decode_char(input[i + 1]);
        const int c2 = decode_char(input[i + 2]);
        const int c3 = decode_char(input[i + 3]);
        if (c0 < 0 || c1 < 0 || c2 == -2 || c3 == -2) {
            return false;
        }

        if (out_len + 3 > output.size()) {
            return false;
        }

        output[out_len++] = static_cast<uint8_t>((c0 << 2) | (c1 >> 4));
        if (c2 >= 0) {
            output[out_len++] = static_cast<uint8_t>((c1 << 4) | (c2 >> 2));
        }
        if (c2 == -1 && c3 >= 0) {
            return false;
        }
        if (c3 >= 0) {
            output[out_len++] = static_cast<uint8_t>((c2 << 6) | c3);
        }
    }

    if (out_len > BUS_OTA_CHUNK_SIZE) {
        return false;
    }
    return true;
}

static void set_response(BusOtaSession &session, const char *status, uint32_t seq, size_t bytes) {
    session.response_length = std::snprintf(session.response, sizeof(session.response),
                                            "%s:%lu:%lu", status,
                                            static_cast<unsigned long>(seq),
                                            static_cast<unsigned long>(bytes));
}

static bool fail(BusOtaSession &session, const char *reason, bool reset = true) {
    session.response_length = std::snprintf(session.response, sizeof(session.response),
                                            "%s:%s", OTA_ERROR_PREFIX, reason);
    if (reset) {
        bus_reset_session(session);
    }
    return true;
}

void bus_reset_session(BusOtaSession &session, bool abort_flash) {
    if (session.handle != 0 && abort_flash) {
        esp_ota_abort(session.handle);
    }
    // Clear OTA state but preserve bus_name (config) and response (for caller to send)
    session.sender = 0;
    session.handle = 0;
    session.partition = nullptr;
    session.next_seq = 0;
    session.bytes_written = 0;
    session.last_activity = 0;
}

bool bus_handle_frame(BusOtaSession &session, uint8_t sender, std::string_view payload) {
    if (!starts_with(payload, "__OTA_")) {
        return false;
    }

    if (starts_with(payload, OTA_BEGIN_PREFIX)) {
        if (session.handle != 0) {
            return fail(session, "busy", false);
        }

        const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
        if (!partition) {
            return fail(session, "no_partition", false);
        }

        esp_ota_handle_t handle = 0;
        if (esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &handle) != ESP_OK) {
            return fail(session, "begin_failed", false);
        }

        session.sender = sender;
        session.handle = handle;
        session.partition = partition;
        session.next_seq = 1;
        session.bytes_written = 0;
        session.last_activity = millis();

        echo("serial bus %s ota start from %u", session.bus_name, sender);
        set_response(session, OTA_READY_PREFIX, session.next_seq, BUS_OTA_CHUNK_SIZE);
        return true;
    }

    if (starts_with(payload, OTA_ABORT_PREFIX)) {
        if (session.handle == 0 || session.sender != sender) {
            return fail(session, "no_session", false);
        }
        return fail(session, "aborted");
    }

    if (starts_with(payload, OTA_COMMIT_PREFIX)) {
        if (session.handle == 0 || session.sender != sender) {
            return fail(session, "no_session", false);
        }
        if (esp_ota_end(session.handle) != ESP_OK) {
            return fail(session, "end_failed");
        }
        if (esp_ota_set_boot_partition(session.partition) != ESP_OK) {
            return fail(session, "boot_failed");
        }

        echo("serial bus %s ota finished (%lu bytes)", session.bus_name, session.bytes_written);
        set_response(session, OTA_DONE_PREFIX, session.next_seq, session.bytes_written);
        bus_reset_session(session, false);
        return true;
    }

    if (starts_with(payload, OTA_CHUNK_PREFIX)) {
        if (session.handle == 0 || session.sender != sender) {
            return fail(session, "no_session", false);
        }

        std::string_view rest = payload.substr(std::strlen(OTA_CHUNK_PREFIX));
        if (rest.empty() || rest.front() != ':') {
            return fail(session, "chunk_format");
        }
        rest.remove_prefix(1);

        const size_t colon_pos = rest.find(':');
        if (colon_pos == std::string_view::npos) {
            return fail(session, "chunk_parts");
        }
        std::string_view seq_view = rest.substr(0, colon_pos);
        std::string_view data_view = rest.substr(colon_pos + 1);
        if (!is_decimal(seq_view)) {
            return fail(session, "chunk_seq");
        }

        const unsigned long seq = std::strtoul(std::string(seq_view).c_str(), nullptr, 10);
        if (seq != session.next_seq || data_view.empty()) {
            return fail(session, "chunk_order");
        }

        std::array<uint8_t, BUS_OTA_BUFFER_SIZE> buffer{};
        size_t decoded_len = 0;
        if (!decode_base64_chunk(data_view, buffer, decoded_len) || decoded_len == 0) {
            return fail(session, "chunk_decode");
        }
        if (esp_ota_write(session.handle, buffer.data(), decoded_len) != ESP_OK) {
            return fail(session, "write_failed");
        }

        session.bytes_written += decoded_len;
        session.next_seq++;
        session.last_activity = millis();
        set_response(session, OTA_ACK_PREFIX, seq, session.bytes_written);
        return true;
    }

    // Status messages from peers; let caller surface them if desired.
    echo("ota[%u] %.*s", sender, static_cast<int>(payload.size()), payload.data());
    return true;
}

void bus_tick(BusOtaSession &session, unsigned long now_ms) {
    if (session.handle != 0 && now_ms - session.last_activity > BUS_OTA_SESSION_TIMEOUT_MS) {
        echo("warning: serial bus %s ota timed out", session.bus_name);
        fail(session, "timeout");
    }
}

} // namespace ota
