#pragma once

namespace bus_backup {

void restore_if_needed();
void save(const int tx_pin, const int rx_pin, const long baud_rate, const int uart_num, const int node_id);
void remove();

} // namespace bus_backup
