# BLE dongle

This standalone firmware turns an ESP32 on the host's USB port into a Bluetooth dongle for the console of a robot running Lizard.
Everything the host writes to the dongle's serial port goes to the robot as it arrives, and the robot's console comes back byte for byte.
The host talks to the dongle as if it were the robot's UART0, so RoSys, `monitor.py` and `configure.py` work unchanged.

Compared to a dongle that runs Lizard with a [BLE bridge](../docs/module_reference.md#ble-bridge), it does not wait for a 10 ms main loop on the dongle.
On a bench with two classic ESP32 the console round trip took about 31 ms at 115200 baud and 23 ms at 460800 baud, compared to 38 ms with the BLE bridge and 16 or 9 ms on the robot's own UART0.
The dongle sends the robot only as much as its console queue can take (the robot reports its progress), and buffers up to 64 KB of the host's input meanwhile.
So even long bursts at 460800 baud arrive completely, which a cable at that rate does not achieve.

## Robot

The robot needs Lizard's [Bluetooth](../docs/module_reference.md#bluetooth) module, e.g. in its startup script:

```
bluetooth = Bluetooth("robot")
```

## Build and flash

Build with ESP-IDF 5.3 and flash the whole image, because the partition table differs from Lizard's:

```bash
cd ble_dongle
idf.py set-target esp32 build
idf.py -p /dev/ttyUSB0 flash
```

To pair with Lizard's developer PIN, build with `SDKCONFIG_DEFAULTS="sdkconfig.defaults;../sdkconfig.defaults.secret"`; otherwise pass the robot's PIN to `!dongle link`.
The serial port starts at 115200 baud (`CONFIG_DONGLE_BAUD_RATE`) until `!dongle baud` stores another rate.

## Link to a robot

Lines starting with `!dongle` stay on the dongle, every other byte goes to the robot:

| Command                         | Effect                                                          |
| ------------------------------- | --------------------------------------------------------------- |
| `!dongle link robot`            | link to the robot advertised as "robot", with the developer PIN |
| `!dongle link robot 123456`     | same, with the robot's user PIN                                 |
| `!dongle link "robot 2" 123456` | a device name with spaces                                       |
| `!dongle unlink`                | stop linking                                                    |
| `!dongle baud 460800`           | switch the serial port to 115200, 230400, 460800 or 921600 baud |
| `!dongle`                       | show the link, the baud rate and the bytes dropped so far       |

The robot's name, the PIN and the baud rate are stored in the dongle's NVS, the bond after the first pairing, so the dongle links again by itself after every restart.
After `!dongle baud`, the host has to reopen the port at the new rate.
It reports its state in lines like `dongle: linked to "robot"`, with a checksum like Lizard's console lines.

## Limits

- The robot's Bluetooth module serves one central at a time, so the app cannot connect while the dongle is linked.
- The serial port to the host has no flow control: input that piles up beyond the 64 KB buffer because the robot cannot keep up is lost.
- Robots with an older Lizard offer no flow control, so lines that arrive faster than they process them are dropped.
- Bytes that arrive while the link is down are dropped; the host notices the broken lines by their checksums.
