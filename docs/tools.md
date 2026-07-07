# Tools

## Installation

### Flash

To install Lizard on your ESP32 run

```bash
sudo ./espresso.py flash --device <device_path>
```

Note that flashing may require root access (hence the sudo).
The command also does not work while the serial interface is busy communicating with another process.

If flashing fails on your USB-UART bridge, you can lower the baud rate with `--baud` (default: 921600).
If the stub upload itself fails (e.g. "Failed to write to target RAM"), pass `--no-stub` to flash via the ROM loader instead.
On boards with a native USB-Serial-JTAG port (e.g. ESP32-S3), prefer that port over an external USB-UART bridge as the most robust way to flash.

### Robot Brain

The `espresso.py` script can also upload firmware on a [Robot Brain](https://www.zauberzeug.com/product-robot-brain.html)
where the microcontroller is connected to the pin header of an NVIDIA Jetson computer.
This requires a Jetson Orin (L4T 35 or 36);
for Nano/Xavier Robot Brains use release 0.12.x or earlier, which still ships `flash.py`.

To flash a Robot Brain over the network from a development machine, pass `--host`.
This rsyncs the binaries and `espresso.py` to the target and runs the command there over SSH:

```bash
./espresso.py flash --host user@robot-brain[:path]
```

`--host` works for every command (`flash`, `erase`, `enable`, `disable`, `reset`, `coredump`),
so e.g. `./espresso.py erase --host user@robot-brain` performs a remote recovery erase.

## Interaction

### Serial Monitor

Use the serial monitor to read the current output and interactively send [Lizard commands](language.md) to the microcontroller.

```bash
./monitor.py [<device_path>]
```

You can also use an SSH monitor to access a microcontroller via SSH:

```bash
./monitor_ssh.sh <user@host>
```

Note that the serial monitor cannot communicate while the serial interface is busy communicating with another process.

### OTB Update

`otb_update.py` pushes firmware to a peer over a `SerialBus` coordinator using the OTB (Over The Bus) protocol.

```bash
./otb_update.py build/lizard.bin --port /dev/ttyUSB0 --target <peer_id> [--bus <name>] [--expander <name>]
```

| Argument     | Description                                           |
| ------------ | ----------------------------------------------------- |
| `firmware`   | Path to the firmware binary (e.g. `build/lizard.bin`) |
| `--port`     | Serial port (default: `/dev/ttyUSB0`)                 |
| `--baud`     | Baudrate (default: `115200`)                          |
| `--target`   | Bus ID of the target node (required)                  |
| `--bus`      | Name of the SerialBus module (default: `bus`)         |
| `--expander` | Expander name when coordinator is behind an expander  |

**Expander chains:**

When the SerialBus coordinator sits behind an expander (e.g. `p0`), pass `--expander p0`.
The script will pause broadcasts on that expander via `core.pause_broadcasts()` before the transfer
and resume them afterwards to keep the UART link clear.

Example:

```bash
./otb_update.py build/lizard.bin --port /dev/ttyUSB0 --target 1 --expander p0
```

This flashes node 1 through expander `p0`.
The target node will reboot with the new firmware after a successful transfer.

**OTB Protocol:**

The OTB (Over The Bus) protocol uses these message types:

| Host → Target              | Description                                                |
| -------------------------- | ---------------------------------------------------------- |
| `__OTB_BEGIN__`            | Begin firmware update session                              |
| `__OTB_CHUNK_<seq>__:data` | Send base64-encoded firmware chunk (incl. sequence number) |
| `__OTB_COMMIT__`           | Commit update and set boot partition                       |
| `__OTB_ABORT__`            | Cancel the update session                                  |

| Host ← Target             | Description                               |
| ------------------------- | ----------------------------------------- |
| `__OTB_ACK_BEGIN__`       | Acknowledge begin                         |
| `__OTB_ACK_CHUNK_<seq>__` | Acknowledge chunk (incl. sequence number) |
| `__OTB_ACK_COMMIT__`      | Acknowledge commit                        |
| `__OTB_ERROR__:reason`    | Error response with reason code           |

Flow:

```
Host                            Target
  |                                |
  |--- __OTB_BEGIN__ ------------->|
  |<-- __OTB_ACK_BEGIN__ ----------|
  |                                |
  |--- __OTB_CHUNK_<0>__:... ----->|
  |<-- __OTB_ACK_CHUNK_<0>__ ------|
  |                                |
  |--- __OTB_CHUNK_<1>__:... ----->|
  |<-- __OTB_ACK_CHUNK_<1>__ ------|
  |                                |
  |       ... more chunks ...      |
  |                                |
  |--- __OTB_CHUNK_<N-1>__:... --->|
  |<-- __OTB_ACK_CHUNK_<N-1>__ ----|
  |                                |
  |--- __OTB_COMMIT__ ------------>|
  |<-- __OTB_ACK_COMMIT__ ---------|
  |                                |
```

On error at any point, the target responds with `__OTB_ERROR__:<message>` with a human-readable error message.

### Configure

Use the configure script to send a new startup script to the microcontroller.

```bash
./configure.py <config_file> <device_path>
```

Note that the configure script cannot communicate while the serial interface is busy communicating with another process.

## Development

### Prepare for Development

Install Python requirements:

```python
python3 -m pip install -r requirements.txt
```

On macOS, use `requirements-macos.txt` instead, which omits `gpiod` (not available on macOS).

Install UART drivers: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers

Get all sub modules:

```bash
git submodule update --init --recursive
```

### Compile Lizard

After making changes to the Lizard language definition or its C++ implementation, you can use the compile script to generate a new parser and executing the compilation in an Espressif IDF Docker container.

```bash
./compile.sh
```

To upload the compiled firmware you can use the `./espresso.py` command described above.

### Backtrace

In case Lizard terminates with a backtrace printed to the serial terminal,
you can translate the addresses to source code lines with the `addr2line` tool from the ESP-IDF toolchain.
Make sure the toolchain is on your `PATH` (e.g. by sourcing `. $IDF_PATH/export.sh`)
and that a compiled ELF file is located at `build/lizard.elf`.
Use the `addr2line` that matches the active ESP-IDF toolchain.
On ESP-IDF 5.2 and newer, the unified Xtensa binary below covers both ESP32 and ESP32-S3.

```bash
xtensa-esp-elf-addr2line -e build/lizard.elf <addresses>
```

### Core Dumps

If Lizard crashes, the ESP32 can store a core dump that you read back over the serial device:

```bash
./espresso.py coredump            # print core dump info
./espresso.py coredump --debug    # start a GDB debug session, then return to the shell
```

This needs a compiled ELF at `build/lizard.elf` and, on the machine reading the dump,
a sourced ESP-IDF environment: `esp_coredump` looks up the core dump partition via
`$IDF_PATH/components/partition_table/parttool.py` and symbolizes with the matching GDB
(`xtensa-esp32-elf-gdb`/`xtensa-esp32s3-elf-gdb`), so the pip package alone is not enough
and it must be importable by `python3` (not just a CLI on the `PATH`).
Add `--host user@robot-brain` to pull a core dump off a Robot Brain without logging in;
the requirements above then apply to the Robot Brain itself, which reads the dump.
Unlike flashing, the remote core dump runs without sudo,
so the SSH user needs access to the serial device (e.g. membership in the `dialout` group).

### Releasing

To build a new release, tag the commit with a "v" prefix, for example "v0.1.4".
A GitHub action will build the binary and create a new release.
After creation you can fill in a description if necessary.
