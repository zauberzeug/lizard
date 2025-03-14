#pragma once
#include <cstdint>

void echo(const char *fmt, ...);
int strip(char *buffer, int len);
int check(char *buffer, int len);

// UART helper functions
void activate_uart_external_mode();
void deactivate_uart_external_mode();
void set_uart_expander_id(const char *id);
bool get_uart_external_mode();
const char *get_uart_expander_id();
void connect_tx_pin();
void disconnect_tx_pin();
