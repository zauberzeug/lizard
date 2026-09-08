#include "frame.h"
#include "driver/uart.h"
#include <cstring>

namespace frame {

// CRC-16/CCITT-FALSE
uint16_t crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }
    return crc;
}

size_t cobs_encode(const uint8_t *input, size_t length, uint8_t *output) {
    size_t read = 0, write = 1, code_pos = 0;
    uint8_t code = 1;
    while (read < length) {
        if (input[read] == 0) {
            output[code_pos] = code;
            code_pos = write++;
            code = 1;
        } else {
            output[write++] = input[read];
            if (++code == 0xff) {
                output[code_pos] = code;
                code_pos = write++;
                code = 1;
            }
        }
        ++read;
    }
    output[code_pos] = code;
    return write;
}

void write(uint8_t src, uint8_t id, uint8_t seq, uint32_t millis, const uint8_t *payload, size_t length) {
    static uint8_t body[MAX_PAYLOAD + 11];
    static uint8_t encoded[sizeof(body) + sizeof(body) / 254 + 3];
    size_t pos = 0;
    body[pos++] = 0; // marker: encodes to 0x01 right after the 0x00 delimiter, text never starts with 0x01
    body[pos++] = src;
    body[pos++] = id;
    body[pos++] = seq;
    memcpy(&body[pos], &millis, 4);
    pos += 4;
    body[pos++] = static_cast<uint8_t>(length);
    memcpy(&body[pos], payload, length);
    pos += length;
    const uint16_t crc = crc16(body, pos);
    memcpy(&body[pos], &crc, 2);
    pos += 2;
    encoded[0] = 0;
    const size_t encoded_length = cobs_encode(body, pos, &encoded[1]);
    encoded[1 + encoded_length] = 0;
    // bypass the VFS console (CRLF translation) and the driver's blocking write (tick-quantized waits):
    // poll the FIFO like the console's printf path does
    const size_t total = encoded_length + 2;
    size_t sent = 0;
    while (sent < total) {
        const int written = uart_tx_chars(UART_NUM_0, reinterpret_cast<const char *>(&encoded[sent]), total - sent);
        if (written > 0) {
            sent += written;
        }
    }
}

} // namespace frame
