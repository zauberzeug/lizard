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

    // Cannot overflow since vsnprintf uses sizeof(buffer) - 1 for n
    pos += printf(&buffer[pos], "\n");

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

int check(char *buffer, int len) {
    len = strip(buffer, len);
    if (len >= 3 && buffer[len - 3] == '@') {
        uint8_t checksum = 0;
        for (int i = 0; i < len - 3; ++i) {
            checksum ^= buffer[i];
        }
        const std::string hex_number(&buffer[len - 2], 2);
        if (std::stoi(hex_number, 0, 16) != checksum) {
            throw std::runtime_error("checksum mismatch");
        }
        len -= 3;
    }
    buffer[len] = 0;
    return len;
}
