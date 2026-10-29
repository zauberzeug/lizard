# Tools

## Installation

### Flash

To install Lizard on your ESP32 run

```bash
sudo ./espresso.py flash [--device <device_path>]
```

Without `--device` the serial device is auto-detected, and you are asked which one to use if several are found.
`--dry-run` prints the commands instead of running them.
It still resolves the serial device, so it needs the adapter attached (or an explicit `--device`).

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
This rsyncs the binaries, `espresso.py` and `serial_devices.py` to the target and runs the command there over SSH:

```bash
./espresso.py flash --host user@robot-brain[:path]
```

`--host` works for every command (`flash`, `erase`, `enable`, `disable`, `reset`, `release_pins`, `coredump`),
so e.g. `./espresso.py erase --host user@robot-brain` performs a remote recovery erase.

## Interaction

### Serial Monitor

Use the serial monitor to read the current output and interactively send [Lizard commands](language.md) to the microcontroller.

```bash
./monitor.py [<device_path>]
```

Without a device path the monitor detects the serial device itself:
on a Robot Brain it is the Jetson's UART to the microcontroller, whatever else is attached via USB,
on a development host it is the attached USB-UART bridge.
If several devices are found, it lists them and asks which one to use.
A microcontroller on a USB-UART bridge attached to a Jetson is not detected and needs its device path.
Pass the device path explicitly when several devices may be found and there is no terminal to answer the question.
Detecting the Jetson's UART has the same Jetson Orin requirement as flashing (see [Robot Brain](#robot-brain)).

The monitor shows [telemetry frames](module_reference.md#core) with the names of their fields:

```
[frame 1 seq=7 millis=123456] motor.position=1.234 motor.axis_state=8 motor.enabled=true
```

A frame it cannot decode shows its [status](#telemetry-decoder) and its payload in hex, e.g. `[frame 1 seq=7 millis=123456 no_layout] b6f39d3f0800000001`.
For a sender with an unknown format version it shows only the first frame.
If the layout of a frame is missing or does not fit, e.g. because the frames started before the monitor, the monitor sends `core.telemetry_info()`, but only 2 s after its last request and the last layout line, so that it does not restart layout lines that are still coming.

You can also use an SSH monitor to access a microcontroller via SSH:

```bash
./monitor_ssh.sh <user@host>
```

This copies `monitor.py`, `serial_devices.py` and `telemetry.py` from this checkout to `~/lizard` on the target and starts the monitor there,
so which device it detects does not depend on the age of the target's own checkout.

Note that the serial monitor cannot communicate while the serial interface is busy communicating with another process.

### Telemetry Decoder

`telemetry.py` decodes [telemetry frames](module_reference.md#core) for the serial monitor and for other host software:

```python
import telemetry

decoder = telemetry.Decoder()
for line in lines:
    text, check = telemetry.split_checksum(line)
    if check is not None and telemetry.checksum(text) != check:
        continue  # a damaged line
    result = decoder.feed(text)
    if isinstance(result, telemetry.Frame) and result.status == 'ok':
        print(result.frame_id, result.seq, result.millis, result.values)
```

`feed` returns a `Text`, a `Layout` or a `Frame` and keeps the layouts of each sender.
`str()` of a frame gives the line the monitor shows, with floats rounded to the seven significant digits of a float32.
A frame that cannot be decoded has `values = None` and one of these statuses:

| Status              | Meaning                                                         |
| ------------------- | --------------------------------------------------------------- |
| `no_layout`         | no layout line with the frame's layout ID has arrived           |
| `layout_incomplete` | layout lines of some fields are missing                         |
| `layout_mismatch`   | the payload size does not match the layout                      |
| `unknown_version`   | the sender's layout lines have a format version other than `v1` |

A host that cannot decode a frame because of its layout, e.g. because the frames started before it, sends `core.telemetry_info()` like the monitor.
When a frame ID gets new fields, e.g. after `core.clear_telemetry()`, its frames carry a new layout ID, and the decoder waits for the layout lines with that ID instead of using the old ones.
Logs should keep the raw lines and decode them when they are read.

### OTB Update

`otb_update.py` pushes firmware to a peer over a `SerialBus` coordinator using the OTB (Over The Bus) protocol.

```bash
./otb_update.py build/lizard.bin [--device <path>] --target <peer_id> [--bus <name>] [--expander <name>]
```

| Argument     | Description                                                    |
| ------------ | -------------------------------------------------------------- |
| `firmware`   | Path to the firmware binary (e.g. `build/lizard.bin`)          |
| `--device`   | Serial device path (default: auto-detected, asks if ambiguous) |
| `--baud`     | Baudrate (default: `115200`)                                   |
| `--target`   | Bus ID of the target node (required)                           |
| `--bus`      | Name of the SerialBus module (default: `bus`)                  |
| `--expander` | Expander name when coordinator is behind an expander           |

**Expander chains:**

When the SerialBus coordinator sits behind an expander (e.g. `p0`), pass `--expander p0`.
The script will pause broadcasts on that expander via `core.pause_broadcasts()` before the transfer
and resume them afterwards to keep the UART link clear.

Example:

```bash
./otb_update.py build/lizard.bin --target 1 --expander p0
```

This flashes node 1 through expander `p0`.
The target node will reboot with the new firmware after a successful transfer.

**OTB Protocol:**

The OTB (Over The Bus) protocol uses these message types:

| Host → Target                      | Description                                                          |
| ---------------------------------- | -------------------------------------------------------------------- |
| `__OTB_BEGIN__`                    | Begin firmware update session                                        |
| `__OTB_CHUNK_<seq>__:<crc>:<data>` | Send base64-encoded firmware chunk (incl. sequence number and CRC32) |
| `__OTB_COMMIT__`                   | Commit update and set boot partition                                 |
| `__OTB_ABORT__`                    | Cancel the update session                                            |

| Host ← Target             | Description                                          |
| ------------------------- | ---------------------------------------------------- |
| `__OTB_ACK_BEGIN__:crc32` | Acknowledge begin (suffix: target checks chunk CRCs) |
| `__OTB_ACK_CHUNK_<seq>__` | Acknowledge chunk (incl. sequence number)            |
| `__OTB_ACK_COMMIT__`      | Acknowledge commit                                   |
| `__OTB_ERROR__:reason`    | Error response with reason code                      |

Flow:

```
Host                              Target
  |                                    |
  |--- __OTB_BEGIN__ ----------------->|
  |<-- __OTB_ACK_BEGIN__:crc32 --------|
  |                                    |
  |--- __OTB_CHUNK_<0>__:<crc>:... --->|
  |<-- __OTB_ACK_CHUNK_<0>__ ----------|
  |                                    |
  |--- __OTB_CHUNK_<1>__:<crc>:... --->|
  |<-- __OTB_ACK_CHUNK_<1>__ ----------|
  |                                    |
  |        ... more chunks ...         |
  |                                    |
  |--- __OTB_CHUNK_<N-1>__:<crc>:... ->|
  |<-- __OTB_ACK_CHUNK_<N-1>__ --------|
  |                                    |
  |--- __OTB_COMMIT__ ---------------->|
  |<-- __OTB_ACK_COMMIT__ -------------|
  |                                    |
```

On error at any point, the target responds with `__OTB_ERROR__:<message>` with a human-readable error message.

**Retransmission and integrity:**
frames can get lost or corrupted on a busy bus (e.g. while the target stalls on flash writes),
so the transfer recovers instead of aborting:
on a gap, duplicate, or corrupted chunk the target re-acknowledges the last chunk it has written
(or `__OTB_ACK_BEGIN__` when nothing arrived yet),
and the host treats such a duplicate acknowledgement — or an acknowledgement timeout — as a signal to rewind and resend from there (go-back-N).
Chunks are only ever written in order, so duplicates are discarded safely;
the session is aborted only when no chunk gets written for 10 s.
Because the bus frames' own checksum is only 8 bits,
each chunk additionally carries a zlib CRC32 over its decimal sequence number followed by its decoded bytes
(`__OTB_CHUNK_<seq>__:<8-hex-crc32>:<base64>`).
The target advertises this with the `:crc32` suffix of its begin acknowledgement;
the host only sends the CRC field to targets that do, so older firmware can still be updated,
and a target still accepts chunks without the field from older hosts.

### Configure

Use the configure script to send a new startup script to the microcontroller.

```bash
./configure.py <config_file> [<device_path>]
```

Without a device path the serial device is detected the same way as for the [serial monitor](#serial-monitor).

Afterwards the configure script restarts the microcontroller and fails if `core.startup_checksum()` does not match the file or the microcontroller does not answer.

Note that the configure script cannot communicate while the serial interface is busy communicating with another process.

## Development

### Prepare for Development

Install Python requirements:

```python
python3 -m pip install -r requirements.txt
```

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

Integrators building Lizard as part of their own project can override the project name,
e.g. `idf.py -DLIZARD_PROJECT_NAME=lizard-zz build`.
The name is reported by `core.version()` and also names the build artifacts (`build/lizard-zz.bin`, `build/lizard-zz.elf`);
`espresso.py` picks them up automatically via `build/project_description.json`,
other tools like `otb_update.py` or `addr2line` need the renamed paths passed explicitly.

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
