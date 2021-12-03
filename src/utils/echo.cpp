#include "echo.h"
#include "driver/uart.h"
#include <cstdarg>
#include <stdio.h>
#include <string>

void echo(const OutputTarget target, const OutputType type, const char *format, ...) {
    static char buffer[1024];
    static char check_buffer[16];

    int pos = 0;
    if (type == text) {
        pos += std::sprintf(buffer, "!\"");
    }
    if (type == code) {
        pos += std::sprintf(buffer, "!!");
    }

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
            if (target & uart0) {
                printf("%s@%02x\n", &buffer[start], checksum);
            }
            if (target & uart1) {
                const int check_len = std::sprintf(check_buffer, "@%02x\n", checksum);
                uart_write_bytes(UART_NUM_1, &buffer[start], i - start);
                uart_write_bytes(UART_NUM_1, check_buffer, check_len);
            }
            start = i + 1;
            checksum = 0;
        } else {
            checksum ^= buffer[i];
        }
    }
}
