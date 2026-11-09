#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

constexpr int CONSOLE_LINE_SIZE = 2048;                     // longest line on the wire in either direction, line ending included
constexpr int CONSOLE_PAYLOAD_SIZE = CONSOLE_LINE_SIZE - 5; // what a line may carry before "@xx\r\n" (stdout ends lines with CRLF)
constexpr long CONSOLE_DEFAULT_BAUD_RATE = 115200;          // also of the ROM bootloader and the boot log

void echo(const char *fmt, ...);

// A serial bus can take over the console UART, e.g. on a port expander wired to the core by its console pins.
// Until it is taken back, nothing but that bus reads or writes UART0: echo() only reaches the echo callbacks,
// and ESP_LOG and ROM output are dropped.
bool console_handed_over();
void hand_over_console(const long baud_rate, const std::function<void(unsigned long timeout_ms)> &drain);
void take_back_console();
// Waits until the bus that took over the console has sent what this node printed, at most `timeout_ms`.
void drain_console(const unsigned long timeout_ms);
// Lines echoed during the boot, for a serial bus that takes over the console before the main loop starts; recording
// ends with the first call.
std::vector<std::string> take_boot_lines();
void check_console_load(int64_t work_us, int64_t period_us); // once per loop cycle, warns when printing keeps it past its period
typedef std::function<void(const char *line)> EchoCallback;
int register_echo_callback(const EchoCallback &callback);
void unregister_echo_callback(const int handle);
int strip(char *buffer, int len);
int check(char *buffer, int len, bool *checksum_ok);
