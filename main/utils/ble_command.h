/*
 * SPDX-FileCopyrightText: 2022 Zauberzeug GmbH
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ZZ_BLE_COMMAND_H
#define ZZ_BLE_COMMAND_H

#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>

namespace ZZ::BleCommand {
// Called on the NimBLE host task with a NUL-terminated copy of the received line.
// The host task's stack is small; the callback must only hand the line off, not parse it.
using CommandCallback = std::function<void(std::unique_ptr<char[]> line)>;

void init(const std::string_view &device_name, CommandCallback on_command);
int send(const std::string_view &data);
// Whether a central is currently connected (regardless of pairing state).
bool is_connected();
void finalize();
void deactivate_pin();
void reset_bonds();

// Called on the NimBLE host task with a console line (without its line end) from an authorized central.
void set_console_callback(CommandCallback on_console_line);
// Whether an authorized central listens to the console output.
bool console_ready();
// Payload of one console notification (ATT MTU - 3).
size_t console_chunk_size();
// Sends the next part of the console byte stream; returns 0 or a NimBLE error, BLE_HS_ENOMEM while buffers are short.
int send_console(const char *data, size_t len);
// NimBLE runs once per node; false if another owner (this service or a console bridge) already uses it.
bool claim_host(const char *owner);

} // namespace ZZ::BleCommand

#endif // ZZ_BLE_COMMAND_H
