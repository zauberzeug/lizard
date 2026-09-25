# Machine Safety

Lizard implements the following features to increase machine safety.

## Checksums

Each line sent via the command-line interface can and should be followed by a checksum.
Lizard will omit any lines with incorrect checksums.
Any output is as well sent with a checksum.
Lines are limited to 2048 bytes in either direction, including the checksum and the line ending.
Lizard discards a longer input line and replaces a longer output line, each with a warning.

The 8-bit checksum is computed as the bitwise XOR of all bytes of the UTF-8 encoded line excluding the newline character and written as a two-digit hex number (with leading zeros) separated with an `@` character, for example:

| Line    | Bitwise XOR                             | Result     |
| ------- | --------------------------------------- | ---------- |
| `1 + 2` | 0x31 ^ 0x20 ^ 0x2b ^ 0x20 ^ 0x32 = 0x28 | `1 + 2@28` |

## Keep-alive signal

The `core` module provides a property `last_message_age`, which holds the time in milliseconds since the last input message was received from the host via UART0 or Bluetooth.
It allows formulating rules that stop critical hardware modules when the connection to the host system is lost.

The following example stops a motor when there is no serial communication for 500 ms:

```
when core.last_message_age > 500 then motor.stop(); end
```

Any message from UART0 or Bluetooth that is not discarded due to an invalid [checksum](#checksums) resets `last_message_age`, even if it cannot be parsed.
Messages received over the [serial bus](module_reference.md#serial-bus) do _not_ reset it, so a peer pushing its state to the core cannot implicitly mask a lost host connection.
If the host controller has no command to send but wants to signal that it is still alive, it can call `core.keep_alive()`, which resets the timer silently without producing any output.
This also works over the serial bus: a coordinator can keep a peer's timer alive by explicitly sending it `core.keep_alive()`.

If the host streams [scheduled blocks](language.md), they should be discarded as well when the connection is lost,
so that no stale commands fire after the host stopped:

```
when core.last_message_age > 500 then motor.stop(); core.clear_schedule() end
```

## Dead man's switch for wheels

The keep-alive rule above has a blind spot: `core.last_message_age` is reset by _any_ input line, on _any_ channel.
A robot that is driven by a remote control over Bluetooth while a host keeps sending heartbeats over UART0 therefore never trips such a rule when the Bluetooth connection drops, and keeps driving with the last speed it received.

The wheels modules ([ODrive Wheels](module_reference.md#odrive-wheels), [RoboClaw Wheels](module_reference.md#roboclaw-wheels) and [DunkerWheels](module_reference.md#dunkerwheels)) therefore carry their own dead man's switch that measures what actually matters: the time since the last drive command to that module, whichever channel it came from.

```
wheels.drive_command_timeout = 0.3
```

After a non-zero `speed()` or `power()` command, the wheels stop on their own once no further drive command arrived for `drive_command_timeout` seconds.
Only drive commands count; `enable()`, `off()` or property writes cannot keep a stale motion alive.
The stop is sent once and logged as a warning, and the next drive command re-arms the switch, so a reconnecting sender has to issue a fresh command before the robot moves again.
The default timeout is 1 s as a defensive baseline; `0` disables the switch, e.g. on a test bench.

Consequently, a host that wants to keep driving has to repeat its drive command at least once per timeout.
A single delayed command can move the robot for at most one timeout.
Choose the timeout as a trade-off between the stopping distance at full speed and false stops on short radio dropouts: a sender that nominally transmits every 100 ms but skips cycles while a write is pending needs a timeout of a few hundred milliseconds.

The `drive_command_age` property (ms) and the Bluetooth module's `connected` and `last_message_age` properties expose the underlying measurements for rules and logging.

## Expander watchdog

The `expander` module provides a watchdog feature that restarts the port expander when it gets stuck and does not send messages anymore.
After `ping_interval` seconds of no messages from the port expander, the `expander` module will instruct the expander to send a "\_\_PONG\_\_" message.
If the expander does not answer within `ping_timeout` seconds, it will be restarted.

The reception of the "\_\_PONG\_\_" message is handled internally by the expander module and is not printed to the serial output.
A similar technique can be used by the main computing unit to check if the core microcontroller is still responsive and restart it otherwise.

If a `proxy` module is active, the `ping_interval` will never elapse, because the `expander` module receives messages via the proxy message handling.
Thus it will not need to ping the expander explicitly.
