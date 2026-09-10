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

size_t build_body(uint8_t src, uint8_t id, uint8_t seq, uint32_t millis, const uint8_t *payload, size_t length, uint8_t *body) {
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
    return pos + 2;
}

bool verify_body(const uint8_t *body, size_t length) {
    if (length < HEADER_SIZE + 2 || body[0] != 0) {
        return false;
    }
    if (static_cast<size_t>(body[HEADER_SIZE - 1]) + HEADER_SIZE + 2 != length) {
        return false;
    }
    uint16_t crc;
    memcpy(&crc, &body[length - 2], 2);
    return crc == crc16(body, length - 2);
}

void write_console(const uint8_t *body, size_t length) {
    static uint8_t encoded[MAX_BODY + MAX_BODY / 254 + 3];
    encoded[0] = 0;
    const size_t encoded_length = cobs_encode(body, length, &encoded[1]);
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

void write(uint8_t src, uint8_t id, uint8_t seq, uint32_t millis, const uint8_t *payload, size_t length) {
    static uint8_t body[MAX_BODY];
    write_console(body, build_body(src, id, seq, millis, payload, length, body));
}

static bool needs_escape(uint8_t byte) {
    return byte == 0x00 || byte == 0x09 || byte == 0x0a || byte == 0x0d || byte == 0x20 || byte == BUS_ESCAPE;
}

size_t bus_stuff(const uint8_t *body, size_t length, char *output, size_t capacity) {
    size_t pos = 0;
    if (capacity < 2) {
        return 0;
    }
    output[pos++] = static_cast<char>(BUS_MARKER);
    for (size_t i = 1; i < length; ++i) { // skip the leading 0x00 marker, the receiver restores it
        if (needs_escape(body[i])) {
            if (pos + 2 >= capacity) {
                return 0;
            }
            output[pos++] = static_cast<char>(BUS_ESCAPE);
            output[pos++] = static_cast<char>(body[i] ^ BUS_ESCAPE_XOR);
        } else {
            if (pos + 1 >= capacity) {
                return 0;
            }
            output[pos++] = static_cast<char>(body[i]);
        }
    }
    output[pos] = '\0';
    return pos;
}

size_t bus_unstuff(const char *input, size_t length, uint8_t *body, size_t capacity) {
    if (length < 1 || static_cast<uint8_t>(input[0]) != BUS_MARKER || capacity < 1) {
        return 0;
    }
    size_t pos = 0;
    body[pos++] = 0;
    for (size_t i = 1; i < length; ++i) {
        uint8_t byte = static_cast<uint8_t>(input[i]);
        if (byte == BUS_ESCAPE) {
            if (++i >= length) {
                return 0;
            }
            byte = static_cast<uint8_t>(input[i]) ^ BUS_ESCAPE_XOR;
        }
        if (pos >= capacity) {
            return 0;
        }
        body[pos++] = byte;
    }
    return pos;
}

} // namespace frame
