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
void finalize();
void deactivate_pin();
void reset_bonds();

} // namespace ZZ::BleCommand

#endif // ZZ_BLE_COMMAND_H
