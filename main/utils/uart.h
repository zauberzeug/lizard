#pragma once

#include <cstdint>

void echo(const char *fmt, ...);
typedef void (*EchoRelayHandler)(uint8_t target, const char *line);
void echo_set_relay_handler(EchoRelayHandler handler);
void echo_set_target(uint8_t target);
int strip(char *buffer, int len);
int check(char *buffer, int len);
