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

/*  */
auto init(const std::string_view &deviceName,
          CommandCallback onCommand) -> void;
/* Sends data to the first connected device via notification.
 * Returns 0 on success, NimBLE error code otherwise. */
auto send(const std::string_view &data) -> int;
auto finalize() -> void;
/* Disable security enforcement (pairing/PIN), allowing unencrypted, unauthenticated access. */
auto deactivate_pin() -> void;
/* Remove all bonded devices from NimBLE store (NVS). */
auto reset_bonds() -> void;
} // namespace ZZ::BleCommand

#endif // ZZ_BLE_COMMAND_H
