#pragma once

#include <cstdint>
#include <functional>
#include <vector>

void echo(const char *fmt, ...);
typedef std::function<void(const char *line)> EchoCallback;
void register_echo_callback(const EchoCallback &callback);
// a module may take UART0 lines before Lizard parses them (e.g. a radio dongle forwarding the console); returns true if consumed
typedef std::function<bool(const char *line, int len)> Uart0Interceptor;
void set_uart0_interceptor(const Uart0Interceptor &interceptor);
bool intercept_uart0(const char *line, int len);
int strip(char *buffer, int len);
int check(char *buffer, int len, bool *checksum_ok);
