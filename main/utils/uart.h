#pragma once
#include <cstdint>

void echo(const char *fmt, ...);
int strip(char *buffer, int len);
int check(char *buffer, int len);

// UART context functions
void set_uart_external_mode(bool mode);
void set_uart_expander_id(const char *id);
bool get_uart_external_mode();
const char *get_uart_expander_id();
