#include "uart.h"
#include <cstdarg>
#include <cstdint>
#include <stdexcept>
#include <stdio.h>
#include <string>

// File-static variables for UART context
static bool uart_external_mode = false;
static char uart_expander_id[2] = {'0', '0'}; // Changed to char[2]
#define ID_TAG '$'                            // Single-byte ASCII character

void set_uart_external_mode(bool mode) { uart_external_mode = mode; }
void set_uart_expander_id(const char *id) {
    uart_expander_id[0] = id[0];
    uart_expander_id[1] = id[1];
}
bool get_uart_external_mode() { return uart_external_mode; }
const char *get_uart_expander_id() { return uart_expander_id; }

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
            if (uart_external_mode) {
                // Calculate checksum including the ID tag and expander ID
                checksum = ID_TAG;
                checksum ^= uart_expander_id[0];
                checksum ^= uart_expander_id[1];
                for (const char *p = &buffer[start]; *p; p++) {
                    checksum ^= *p;
                }
                printf("%c%c%c%s@%02x\n", ID_TAG, uart_expander_id[0], uart_expander_id[1],
                       &buffer[start], checksum);
            } else {
                printf("%s@%02x\n", &buffer[start], checksum);
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
