#ifndef ESP32_SERIAL_REPLICATOR_H
#define ESP32_SERIAL_REPLICATOR_H

namespace ZZ::Replicator {

/* Clones the current flash image, up until the end of the last partition,
 * onto the target connected via UART1. Returns true on success.
 * On failure, returns false and prints a message detailing what went wrong
 * to the error log. */
auto flashReplica() -> bool;

} // namespace ZZ::Replicator

#endif // ESP32_SERIAL_REPLICATOR_H
