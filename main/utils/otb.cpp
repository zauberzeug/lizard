#include "otb.h"
#include "mbedtls/base64.h"
#include "timing.h"
#include "uart.h"
#include <cstdio>
#include <cstring>

namespace otb {

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

static void bus_reset_session(BusOtbSession &session) {
    session.sender = 0;
    session.handle = 0;
    session.partition = nullptr;
    session.next_seq = 0;
    session.bytes_written = 0;
    session.last_activity = 0;
}

static bool fail(BusOtbSession &session, uint8_t receiver, const char *reason) {
    respond(session, receiver, "%s:%s", OTB_ERROR_PREFIX, reason);
    esp_ota_abort(session.handle);
    bus_reset_session(session);
    return true;
}

bool bus_handle_frame(BusOtbSession &session, uint8_t sender, std::string_view msg) {
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
        if (!session.handle || session.sender != sender) {
            respond(session, sender, "%s:no_session", OTB_ERROR_PREFIX);
            return true;
        }
        return fail(session, sender, "aborted");
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
        bus_reset_session(session);
        return true;
    }

    // __OTB_CHUNK_{seq}__:{base64}
    if (std::strncmp(msg.data(), OTB_CHUNK_PREFIX, strlen(OTB_CHUNK_PREFIX)) == 0) {
        if (!session.handle || session.sender != sender) {
            respond(session, sender, "%s:no_session", OTB_ERROR_PREFIX);
            return true;
        }

        const std::string_view rest = msg.substr(strlen(OTB_CHUNK_PREFIX));
        const size_t sep = rest.find("__:");
        if (sep == std::string_view::npos) {
            return fail(session, sender, "format");
        }

        char *end;
        const unsigned long seq = std::strtoul(rest.data(), &end, 10);
        if (end != rest.data() + sep || seq != session.next_seq) {
            return fail(session, sender, "seq");
        }

        const std::string_view b64 = rest.substr(sep + 3);
        uint8_t buf[BUS_OTB_BUFFER_SIZE];
        size_t len;
        const int err = mbedtls_base64_decode(buf, sizeof(buf), &len, reinterpret_cast<const unsigned char *>(b64.data()), b64.size());
        if (err != 0 || len == 0 || len > BUS_OTB_CHUNK_SIZE) {
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

    respond(session, sender, "%s:unknown", OTB_ERROR_PREFIX);
    return true;
}

void bus_tick(BusOtbSession &session) {
    if (session.handle && millis() - session.last_activity > BUS_OTB_SESSION_TIMEOUT_MS) {
        echo("warning: serial bus %s otb timed out", session.bus_name);
        fail(session, session.sender, "timeout");
    }
}

} // namespace otb
