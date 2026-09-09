#pragma once

#include "esp_ota_ops.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace otb {

// OTB (Over The Bus) protocol message prefixes
constexpr const char OTB_MSG_PREFIX[] = "__OTB_";
constexpr const char OTB_BEGIN_PREFIX[] = "__OTB_BEGIN__";
constexpr const char OTB_CHUNK_PREFIX[] = "__OTB_CHUNK_";
constexpr const char OTB_COMMIT_PREFIX[] = "__OTB_COMMIT__";
constexpr const char OTB_ABORT_PREFIX[] = "__OTB_ABORT__";
constexpr const char OTB_ACK_PREFIX[] = "__OTB_ACK_";
constexpr const char OTB_ACK_BEGIN[] = "__OTB_ACK_BEGIN__:crc32"; // the suffix advertises per-chunk CRC support
constexpr const char OTB_ACK_CHUNK_PREFIX[] = "__OTB_ACK_CHUNK_";
constexpr const char OTB_ACK_COMMIT[] = "__OTB_ACK_COMMIT__";
constexpr const char OTB_ERROR_PREFIX[] = "__OTB_ERROR__";

// A chunk line "__OTB_CHUNK_<seq>__:<8-hex-crc32>:<base64>" must fit the bus payload
// (checked by a static_assert in serial_bus.cpp; keep CHUNK_SIZE in otb_update.py in sync)
constexpr size_t BUS_OTB_CHUNK_SIZE = 165;
constexpr size_t BUS_OTB_MAX_SEQ_DIGITS = 5;
constexpr size_t BUS_OTB_CHUNK_LINE_SIZE =
    sizeof(OTB_CHUNK_PREFIX) - 1 + BUS_OTB_MAX_SEQ_DIGITS + 3 + 9 + (BUS_OTB_CHUNK_SIZE + 2) / 3 * 4;
constexpr size_t BUS_OTB_WINDOW = 8; // unacked chunks in flight, must match WINDOW in otb_update.py
constexpr size_t BUS_OTB_BUFFER_SIZE = 256;
constexpr unsigned long BUS_OTB_SESSION_TIMEOUT_MS = 10000;

constexpr size_t OTB_RESPONSE_SIZE = 64;

using SendFn = std::function<void(uint8_t receiver, const char *data, size_t len)>;

struct BusOtbSession {
    uint8_t sender = 0;
    esp_ota_handle_t handle = 0;
    const esp_partition_t *partition = nullptr;
    uint32_t next_seq = 0;
    bool uses_crc = false;
    size_t bytes_written = 0;
    unsigned long last_activity = 0;
    const char *bus_name = nullptr;
    SendFn send_fn;
};

bool bus_handle_frame(BusOtbSession &session, uint8_t sender, std::string_view payload);
void bus_tick(BusOtbSession &session);

} // namespace otb
