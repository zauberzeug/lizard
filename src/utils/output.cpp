#include "output.h"

#include <cstdarg>
#include <stdio.h>
#include <string>
#include "driver/uart.h"

void echo(OutputTarget target, OutputType type, const char *format, ...)
{
    static char buffer[1024];

    int pos = std::sprintf(buffer, type == text ? "!\"" : "!!");

    va_list args;
    va_start(args, format);
    pos += std::vsnprintf(&buffer[pos], sizeof buffer - pos, format, args);
    va_end(args);

    uint8_t checksum = 0;
    for (unsigned int i = 0; i < sizeof buffer and buffer[i] > 0; ++i)
    {
        checksum ^= buffer[i];
    }
    pos += std::sprintf(&buffer[pos], "@%02x\n", checksum);

    if (target & uart0)
    {
        printf(buffer);
    }
    if (target & uart1)
    {
        uart_write_bytes(UART_NUM_1, buffer, pos);
    }
}
