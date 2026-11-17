/*
 * SPDX-FileCopyrightText: 2022 Zauberzeug GmbH
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ZZ_BLE_COMMAND_H
#define ZZ_BLE_COMMAND_H

#include <cstdint>
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

enum class ScanOutcome : uint8_t {
    found,
    not_found,
    aborted, // the scan ended before its time was up, e.g. because the host reset
};

struct ScanResult {
    ScanOutcome outcome;
    const char *name;   // the name the scan looked for; valid until the next scan starts
    uint8_t address[6]; // address of the device found, most significant byte first
    int8_t rssi;        // signal strength of the matching packet (dBm)
};

// Look for a device advertising `name` for up to `duration_ms`, without connecting to it.
// Returns nullptr if the scan started, else the reason why not (e.g. a scan is running or the stack is not ready).
const char *scan(const std::string_view &name, uint32_t duration_ms);
// Hand out the result of a finished scan, once. Call it from the main task; the next scan can start afterwards.
bool take_scan_result(ScanResult &result);

} // namespace ZZ::BleCommand

#endif // ZZ_BLE_COMMAND_H
