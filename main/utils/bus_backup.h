#pragma once

#include <cstdint>

namespace bus_backup {

void save_if_present();
void restore_if_needed();
// The baud rate of a backed-up bus on the console pins; false, and `baud_rate` unchanged, without one.
bool get_console_baud_rate(uint32_t &baud_rate);
void remove();

} // namespace bus_backup
