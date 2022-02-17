#include "uart.h"
#include <cstdarg>
#include <stdexcept>
#include <stdio.h>
#include <string>

void echo(const char *format, ...) {
    static char buffer[1024];
    int pos = 0;

    va_list args;
    va_start(args, format);
    pos += std::vsnprintf(&buffer[pos], sizeof buffer - pos - 1, format, args);
    va_end(args);

    pos += std::sprintf(&buffer[pos], "\n");

    uint8_t checksum = 0;
    int start = 0;
    for (unsigned int i = 0; i < pos; ++i) {
        if (buffer[i] == '\n') {
            buffer[i] = '\0';
            printf("%s@%02x\n", &buffer[start], checksum);
            start = i + 1;
            checksum = 0;
        } else {
            checksum ^= buffer[i];
        }
    }
}

int check(const char *buffer, int len) {
    int suffix = 0;
    if (len >= 5 && buffer[len - 2] == '\r' && buffer[len - 5] == '@') {
        suffix = 5;
    } else if (len >= 4 && buffer[len - 4] == '@') {
        suffix = 4;
    }
    if (suffix) {
        uint8_t checksum = 0;
        for (int i = 0; i < len - suffix; ++i) {
            checksum ^= buffer[i];
        }
        const std::string hex_number(&buffer[len - suffix + 1], 2);
        if (std::stoi(hex_number, 0, 16) != checksum) {
            throw std::runtime_error("checksum mismatch");
        }
        len -= suffix;
    } else {
        len -= 1;
    }
    return len;
}