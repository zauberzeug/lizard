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

    // TODO: Properly handle buffer overflows
    make_check_line(buffer, pos);
    print(buffer);
}

int strip(char *buffer, int len) {
    while (buffer[len - 1] == ' ' ||
           buffer[len - 1] == '\t' ||
           buffer[len - 1] == '\r' ||
           buffer[len - 1] == '\n') {
        len--;
    }
    buffer[len] = 0;
    return len;
}

int make_checked_line(char *buffer, int len) {
    uint8_t checksum = 0;
    for (unsigned int i = 0; i < len; ++i) {
        
        if (buffer[i] == '\0') {
            sprintf(&buffer[i], "@%02x", checksum);
            
        } else if (buffer[i] == '\n') {
            sprintf(&buffer[i], "@%02x\n", checksum);
            
        } else {
            checksum ^= buffer[i];
        }
    }
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
