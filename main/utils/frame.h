#pragma once

#include <cstddef>
#include <cstdint>

// Binary telemetry frame: 0x00 | COBS(0x00 src id seq millis[4] len payload crc16[2]) | 0x00
namespace frame {

constexpr size_t MAX_PAYLOAD = 200;

uint16_t crc16(const uint8_t *data, size_t length);
size_t cobs_encode(const uint8_t *input, size_t length, uint8_t *output);
void write(uint8_t src, uint8_t id, uint8_t seq, uint32_t millis, const uint8_t *payload, size_t length);

} // namespace frame
