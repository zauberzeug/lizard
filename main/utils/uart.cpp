#include "uart.h"
#include <cstdarg>
#include <stdexcept>
#include <stdio.h>
#include <string>

void echo(const char *format, ...) {
    static char buffer[1024];

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

static int parse_hex_byte(const char high, const char low, bool &ok) {
    auto parse_nibble = [&](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        } else if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        ok = false;
        return 0;
    };

    const int high_val = parse_nibble(high);
    const int low_val = parse_nibble(low);
    if (!ok) {
        return 0;
    }
    return (high_val << 4) | low_val;
}

int check(char *buffer, int len, bool *checksum_ok) {
    len = strip(buffer, len);
    bool ok = true;
    if (len >= 3 && buffer[len - 3] == '@') {
        uint8_t checksum = 0;
        for (int i = 0; i < len - 3; ++i) {
            checksum ^= buffer[i];
        }
        const int parsed_checksum = parse_hex_byte(buffer[len - 2], buffer[len - 1], ok);
        if (!ok || parsed_checksum != checksum) {
            ok = false;
        } else {
            len -= 3;
        }
    }
    buffer[len] = 0;
    if (checksum_ok != nullptr) {
        *checksum_ok = ok;
    } else if (!ok) {
        throw std::runtime_error("checksum mismatch");
    }
    return len;
}
