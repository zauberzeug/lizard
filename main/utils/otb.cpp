#include "otb.h"
#include "timing.h"
#include "uart.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace otb {

static bool decode_base64(std::string_view input, uint8_t *output, size_t max_len, size_t &out_len) {
    out_len = 0;
    if (input.empty() || input.size() % 4 != 0) {
        return false;
    }

    auto decode_char = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        if (c == '=') return -1;
        return -2;
    };

    for (size_t i = 0; i < input.size(); i += 4) {
        int c0 = decode_char(input[i]);
        int c1 = decode_char(input[i + 1]);
        int c2 = decode_char(input[i + 2]);
        int c3 = decode_char(input[i + 3]);
        if (c0 < 0 || c1 < 0 || c2 == -2 || c3 == -2 || (c2 == -1 && c3 >= 0)) {
            return false;
        }
        if (out_len >= max_len) {
            return false;
        }
        output[out_len++] = (c0 << 2) | (c1 >> 4);
        if (c2 >= 0 && out_len < max_len) {
            output[out_len++] = (c1 << 4) | (c2 >> 2);
        }
        if (c3 >= 0 && out_len < max_len) {
            output[out_len++] = (c2 << 6) | c3;
        }
    }
    return true;
}

static void respond(BusOtbSession &session, uint8_t receiver, const char *fmt, ...) {
    char buf[OTB_RESPONSE_SIZE];
    va_list args;
    va_start(args, fmt);
    const int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (session.send_fn && len > 0) {
        session.send_fn(receiver, buf, len);
    }
}

static bool fail(BusOtbSession &session, uint8_t receiver, const char *reason) {
    respond(session, receiver, "%s:%s", OTB_ERROR_PREFIX, reason);
    bus_reset_session(session);
    return true;
}

bool bus_handle_frame(BusOtbSession &session, uint8_t sender, std::string_view msg) {
    if (std::strncmp(msg.data(), OTB_MSG_PREFIX, strlen(OTB_MSG_PREFIX)) != 0) {
        return false;
    }

    // __OTB_BEGIN__
    if (msg == OTB_BEGIN_PREFIX) {
        if (session.handle) {
            respond(session, sender, "%s:busy", OTB_ERROR_PREFIX);
            return true;
        }
        const esp_partition_t *part = esp_ota_get_next_update_partition(nullptr);
        if (!part || esp_ota_begin(part, OTA_SIZE_UNKNOWN, &session.handle) != ESP_OK) {
            respond(session, sender, "%s:begin_failed", OTB_ERROR_PREFIX);
            return true;
        }
        session.sender = sender;
        session.partition = part;
        session.next_seq = 0;
        session.bytes_written = 0;
        session.last_activity = millis();
        echo("serial bus %s otb start from %u", session.bus_name, sender);
        respond(session, sender, OTB_ACK_BEGIN);
        return true;
    }

    // __OTB_ABORT__
    if (msg == OTB_ABORT_PREFIX) {
        return session.handle && session.sender == sender ? fail(session, sender, "aborted") : true;
    }

    // __OTB_COMMIT__
    if (msg == OTB_COMMIT_PREFIX) {
        if (!session.handle || session.sender != sender) {
            respond(session, sender, "%s:no_session", OTB_ERROR_PREFIX);
            return true;
        }
        if (esp_ota_end(session.handle) != ESP_OK || esp_ota_set_boot_partition(session.partition) != ESP_OK) {
            return fail(session, sender, "commit_failed");
        }
        echo("serial bus %s otb finished (%lu bytes)", session.bus_name, static_cast<unsigned long>(session.bytes_written));
        respond(session, sender, OTB_ACK_COMMIT);
        bus_reset_session(session, false);
        return true;
    }

    // __OTB_CHUNK_{seq}__:{base64}
    if (std::strncmp(msg.data(), OTB_CHUNK_PREFIX, strlen(OTB_CHUNK_PREFIX)) == 0) {
        if (!session.handle || session.sender != sender) {
            respond(session, sender, "%s:no_session", OTB_ERROR_PREFIX);
            return true;
        }

        std::string_view rest = msg.substr(strlen(OTB_CHUNK_PREFIX));
        size_t sep = rest.find("__:");
        if (sep == std::string_view::npos) {
            return fail(session, sender, "format");
        }

        char *end;
        unsigned long seq = std::strtoul(rest.data(), &end, 10);
        if (end != rest.data() + sep || seq != session.next_seq) {
            return fail(session, sender, "seq");
        }

        std::string_view b64 = rest.substr(sep + 3);
        uint8_t buf[BUS_OTB_BUFFER_SIZE];
        size_t len;
        if (!decode_base64(b64, buf, sizeof(buf), len) || len == 0 || len > BUS_OTB_CHUNK_SIZE) {
            return fail(session, sender, "decode");
        }
        if (esp_ota_write(session.handle, buf, len) != ESP_OK) {
            return fail(session, sender, "write");
        }

        session.bytes_written += len;
        session.next_seq++;
        session.last_activity = millis();
        respond(session, sender, "%s%lu__", OTB_ACK_CHUNK_PREFIX, seq);
        return true;
    }

    echo("otb[%u] %.*s", sender, static_cast<int>(msg.size()), msg.data());
    return true;
}

void bus_reset_session(BusOtbSession &session, bool abort_flash) {
    if (session.handle && abort_flash) {
        esp_ota_abort(session.handle);
    }
    session.sender = 0;
    session.handle = 0;
    session.partition = nullptr;
    session.next_seq = 0;
    session.bytes_written = 0;
    session.last_activity = 0;
}

void bus_tick(BusOtbSession &session, unsigned long now_ms) {
    if (session.handle && now_ms - session.last_activity > BUS_OTB_SESSION_TIMEOUT_MS) {
        echo("warning: serial bus %s otb timed out", session.bus_name);
        fail(session, session.sender, "timeout");
    }
}

} // namespace otb
