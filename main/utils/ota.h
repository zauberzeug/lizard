#pragma once

#include "esp_ota_ops.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace ota {

constexpr const char OTA_BEGIN_PREFIX[] = "__OTA_BEGIN__";
constexpr const char OTA_CHUNK_PREFIX[] = "__OTA_CHUNK__";
constexpr const char OTA_COMMIT_PREFIX[] = "__OTA_COMMIT__";
constexpr const char OTA_ABORT_PREFIX[] = "__OTA_ABORT__";
constexpr const char OTA_READY_PREFIX[] = "__OTA_READY__";
constexpr const char OTA_ACK_PREFIX[] = "__OTA_ACK__";
constexpr const char OTA_DONE_PREFIX[] = "__OTA_DONE__";
constexpr const char OTA_ERROR_PREFIX[] = "__OTA_ERROR__";

constexpr size_t BUS_OTA_CHUNK_SIZE = 174;
constexpr size_t BUS_OTA_BUFFER_SIZE = 256;
constexpr unsigned long BUS_OTA_SESSION_TIMEOUT_MS = 10000;

struct BusOtaSession {
    uint8_t sender = 0;
    esp_ota_handle_t handle = 0;
    const esp_partition_t *partition = nullptr;
    uint32_t next_seq = 0;
    size_t bytes_written = 0;
    size_t declared_size = 0;
    unsigned long last_activity = 0;
};

using SendCallback = std::function<void(uint8_t receiver, const char *payload, size_t length)>;

void bus_reset_session(BusOtaSession &session, bool abort_flash = true);

bool bus_handle_frame(BusOtaSession &session,
                      uint8_t sender,
                      std::string_view payload,
                      const char *bus_name,
                      const SendCallback &send_cb);

void bus_tick(BusOtaSession &session,
              unsigned long now_ms,
              const char *bus_name,
              const SendCallback &send_cb);

} // namespace ota
