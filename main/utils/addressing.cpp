#include "addressing.h"

static bool uart_external_mode = false;
static char uart_expander_id = '0';

void activate_uart_external_mode() {
    uart_external_mode = true;
}

void deactivate_uart_external_mode() {
    uart_external_mode = false;
}

bool get_uart_external_mode() {
    return uart_external_mode;
}

void set_uart_expander_id(const char id) {
    uart_expander_id = id;
}

char get_uart_expander_id() {
    return uart_expander_id;
}
