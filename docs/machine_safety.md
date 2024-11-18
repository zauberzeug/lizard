# Machine Safety

Lizard implements the following features to increase machine safety.

## Checksums

Each line sent via the command-line interface can and should be followed by a checksum.
Lizard will omit any lines with incorrect checksums.
Any output is as well sent with a checksum.

The 8-bit checksum is computed as the bitwise XOR of all characters excluding the newline character and written as a two-digit hex number (with leading zeros) separated with an `@` character, for example:

| Line    | Bitwise XOR                             | Result     |
| ------- | --------------------------------------- | ---------- |
| `1 + 2` | 0x31 ^ 0x20 ^ 0x2b ^ 0x20 ^ 0x32 = 0x28 | `1 + 2@28` |

## Keep-alive signal

The `core` module provides a property `last_message_age`, which holds the time in milliseconds since the last input message was received from UART0, parsed and successfully interpreted.
It allows formulating rules that stop critical hardware modules when the connection to the host system is lost.

The following example stops a motor when there is no serial communication for 500 ms:

    when core.last_message_age > 500 then motor.stop(); end

## Expander watchdog

The expander module provides a watchdog feature that restarts the expander ESP32 when it gets stuck and does not respond to pings anymore.
After `ping_interval` seconds of no messages from the expander ESP32, the expander module will ping the expander ESP32 with a `__PING__` message.
If the expander module does not notice the reception of the `__PING__` message within `ping_timeout` seconds, it will restart the expander ESP32.

The reception of the `__PING__` message is not visible in the serial output and will be handled internally by the expander module.
A similar mechanism could be used by the main computing unit to check if the core ESP32 is stuck and restart it.

If a proxy is active, the expander module will receive messages via the proxy message handling and will not ping the expander ESP32, since the `ping_interval` will never elapse.
