/*
 * SPDX-FileCopyrightText: 2022 Zauberzeug GmbH
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ZZ_BLE_COMMAND_H
#define ZZ_BLE_COMMAND_H

#include <functional>
#include <string_view>

namespace ZZ::BleCommand {
using CommandCallback = std::function<void(const std::string_view &)>;

void init(const std::string_view &device_name, CommandCallback on_command);
int send(const std::string_view &data);
void finalize();
void deactivate_pin();
void reset_bonds();

} // namespace ZZ::BleCommand

#endif // ZZ_BLE_COMMAND_H
