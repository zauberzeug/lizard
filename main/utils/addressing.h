#pragma once

static constexpr char ID_TAG = '$';

void activate_uart_external_mode();
void deactivate_uart_external_mode();
bool get_uart_external_mode();
void set_uart_expander_id(const char id);
char get_uart_expander_id();
