#include "uart.h"
#include "driver/uart.h"
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <stdexcept>
#include <stdio.h>
#include <string>

static std::vector<EchoCallback> echo_callbacks;

void register_echo_callback(const EchoCallback &callback) {
    echo_callbacks.push_back(callback);
}

void echo(const char *format, ...) {
    static char buffer[1024];
    static char line[sizeof(buffer) + 8];

    va_list args;
    va_start(args, format);
    const int num_chars = std::vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    int pos = std::min(num_chars, static_cast<int>(sizeof(buffer) - 2));
    va_end(args);

    pos += std::sprintf(&buffer[pos], "\n");

    uint8_t checksum = 0;
    int start = 0;
    for (unsigned int i = 0; i < pos; ++i) {
        if (buffer[i] == '\n') {
            buffer[i] = '\0';
            // write to the driver's TX ring buffer instead of printf (busy-wait);
            // returns immediately, the UART ISR sends in the background
            const int len = std::snprintf(line, sizeof(line), "%s@%02x\n", &buffer[start], checksum);
            uart_write_bytes(UART_NUM_0, line, len);
            for (const auto &callback : echo_callbacks) {
                callback(&buffer[start]);
            }
            start = i + 1;
            checksum = 0;
        } else {
            checksum ^= buffer[i];
        }
    }
}

int strip(char *buffer, int len) {
    while (len > 0 &&
           (buffer[len - 1] == ' ' ||
            buffer[len - 1] == '\t' ||
            buffer[len - 1] == '\r' ||
            buffer[len - 1] == '\n')) {
        len--;
    }
    buffer[len] = 0;
    return len;
}

int check(char *buffer, int len, bool *checksum_ok) {
    len = strip(buffer, len);
    bool ok = true;
    if (len >= 3 && buffer[len - 3] == '@') {
        uint8_t checksum = 0;
        for (int i = 0; i < len - 3; ++i) {
            checksum ^= buffer[i];
        }
        const std::string hex_number(&buffer[len - 2], 2);
        if (std::stoi(hex_number, 0, 16) != checksum) {
            ok = false;
        } else {
            len -= 3;
        }
    }
    buffer[len] = 0;
    *checksum_ok = ok;
    return len;
}
