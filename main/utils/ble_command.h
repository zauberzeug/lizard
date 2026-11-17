/*
 * SPDX-FileCopyrightText: 2022 Zauberzeug GmbH
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ZZ_BLE_COMMAND_H
#define ZZ_BLE_COMMAND_H

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
// Look for a device advertising `name` for up to `duration_ms`, without connecting to it.
// Prints one line when the scan ends: `BLE scan: found "<name>" <address> rssi=<dBm>` or `BLE scan: "<name>" not found`.
// Returns nullptr if the scan started, else the reason why not (e.g. a scan is running or the stack is still starting).
const char *scan(const std::string_view &name, uint32_t duration_ms);

} // namespace ZZ::BleCommand

#endif // ZZ_BLE_COMMAND_H
