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
constexpr const char OTB_ACK_BEGIN[] = "__OTB_ACK_BEGIN__";
constexpr const char OTB_ACK_CHUNK_PREFIX[] = "__OTB_ACK_CHUNK_";
constexpr const char OTB_ACK_COMMIT[] = "__OTB_ACK_COMMIT__";
constexpr const char OTB_ERROR_PREFIX[] = "__OTB_ERROR__";

constexpr size_t BUS_OTB_CHUNK_SIZE = 174;
constexpr size_t BUS_OTB_BUFFER_SIZE = 256;
constexpr unsigned long BUS_OTB_SESSION_TIMEOUT_MS = 10000;

constexpr size_t OTB_RESPONSE_SIZE = 64;

using SendFn = std::function<void(uint8_t receiver, const char *data, size_t len)>;

struct BusOtbSession {
    uint8_t sender = 0;
    esp_ota_handle_t handle = 0;
    const esp_partition_t *partition = nullptr;
    uint32_t next_seq = 0;
    size_t bytes_written = 0;
    unsigned long last_activity = 0;
    const char *bus_name = nullptr;
    SendFn send_fn;
};

bool bus_handle_frame(BusOtbSession &session, uint8_t sender, std::string_view payload);
void bus_tick(BusOtbSession &session);

} // namespace otb
