#pragma once

#include <cstddef>
#include <cstdint>

// Binary telemetry frame. Body: 0x00 | src | id | seq | millis[4] | len | payload[len] | crc16[2]
// (CRC-16/CCITT-FALSE over everything before it). On the console the body travels as 0x00 | COBS(body) | 0x00;
// on the serial bus (line based, payload is a C string) as 0x01 | byte-stuffed body without the leading 0x00.
namespace frame {

constexpr size_t MAX_PAYLOAD = 200;
constexpr size_t HEADER_SIZE = 9; // marker src id seq millis[4] len
constexpr size_t MAX_BODY = HEADER_SIZE + MAX_PAYLOAD + 2;
constexpr uint8_t BUS_MARKER = 0x01;
constexpr uint8_t BUS_ESCAPE = 0x7d;
constexpr uint8_t BUS_ESCAPE_XOR = 0x50;

uint16_t crc16(const uint8_t *data, size_t length);
size_t cobs_encode(const uint8_t *input, size_t length, uint8_t *output);

// assemble a body into `body` (at least MAX_BODY bytes); returns its length
size_t build_body(uint8_t src, uint8_t id, uint8_t seq, uint32_t millis, const uint8_t *payload, size_t length, uint8_t *body);
// length field and CRC of a complete body are consistent
bool verify_body(const uint8_t *body, size_t length);
// write a complete body to the console as 0x00 | COBS(body) | 0x00
void write_console(const uint8_t *body, size_t length);
// build and write in one go (console sink)
void write(uint8_t src, uint8_t id, uint8_t seq, uint32_t millis, const uint8_t *payload, size_t length);

// bus transport: 0x01 marker plus the body (without its leading 0x00) with 0x00/0x09/0x0a/0x0d/0x20/0x7d escaped
// as 0x7d, byte ^ 0x50; the result never contains those bytes, so it survives the bus' C-string handling and strip()
size_t bus_stuff(const uint8_t *body, size_t length, char *output, size_t capacity);
// inverse of bus_stuff; returns the body length (with leading 0x00 restored) or 0 on malformed input
size_t bus_unstuff(const char *input, size_t length, uint8_t *body, size_t capacity);

} // namespace frame
