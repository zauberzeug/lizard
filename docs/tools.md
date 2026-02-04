# Tools

## Installation

### Flash

To install Lizard on your ESP32 run

```bash
sudo ./espresso.py flash <device_path>
```

Note that flashing may require root access (hence the sudo).
The command also does not work while the serial interface is busy communicating with another process.

### Robot Brain

The `espresso.py` script can also upload firmware on a [Robot Brain](https://www.zauberzeug.com/product-robot-brain.html)
where the microcontroller is connected to the pin header of an NVIDIA Jetson computer.

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

`otb_update.py` pushes firmware to a peer over a `SerialBus` coordinator using the OTB protocol.

```bash
./otb_update.py build/lizard.bin --port /dev/ttyUSB0 --id <peer_id> [--bus <name>] [--expander <name>]
```

| Argument     | Description                                           |
| ------------ | ----------------------------------------------------- |
| `firmware`   | Path to the firmware binary (e.g. `build/lizard.bin`) |
| `--port`     | Serial port (default: `/dev/ttyUSB0`)                 |
| `--baud`     | Baudrate (default: `115200`)                          |
| `--id`       | Bus ID of the target node (required)                  |
| `--bus`      | Name of the SerialBus module (default: `bus`)         |
| `--expander` | Expander name when coordinator is behind an expander  |

**Expander chains:**

When the SerialBus coordinator sits behind an expander (e.g. `p0`), pass `--expander p0`.
The script will pause broadcasts on that expander via `core.pause_broadcasts()` before the transfer
and resume them afterwards to keep the UART link clear.

**Example with expander:**

```bash
./otb_update.py build/lizard.bin --port /dev/ttyUSB0 --id 1 --expander p0
```

This flashes node 1 through expander `p0`.
The target node will reboot with the new firmware after a successful transfer.

**OTB Protocol:**

The OTB (Over The Bus) protocol uses these message types:

| Host → Target              |
| -------------------------- | ---------------------------------------------------------- |
| `__OTB_BEGIN__`            | Begin firmware update session                              |
| `__OTB_CHUNK_<seq>__:data` | Send base64-encoded firmware chunk (incl. sequence number) |
| `__OTB_COMMIT__`           | Commit update and set boot partition                       |
| `__OTB_ABORT__`            | Cancel the update session                                  |

| Host ← Target                  | Description                               |
| ------------------------------ | ----------------------------------------- |
| `__OTB_ACK_BEGIN__`            | Acknowledge begin                         |
| `__OTB_ACK_CHUNK_<seq>__:data` | Acknowledge chunk (incl. sequence number) |
| `__OTB_ACK_COMMIT__`           | Acknowledge commit                        |
| `__OTB_ERROR__:reason`         | Error response with reason code           |

**Protocol flow:**

```
Host                            Target
  |                                |
  |--- __OTB_BEGIN__ ------------->|
  |<-- __OTB_ACK_BEGIN__ ----------|
  |                                |
  |--- __OTB_CHUNK_<0>__:... ----->|
  |<-- __OTB_ACK_<0>__ ------------|
  |                                |
  |--- __OTB_CHUNK_<1>__:... ----->|
  |<-- __OTB_ACK_<1>__ ------------|
  |                                |
  |       ... more chunks ...      |
  |                                |
  |--- __OTB_CHUNK_<N-1>__:... --->|
  |<-- __OTB_ACK_<N-1>__ ----------|
  |                                |
  |--- __OTB_COMMIT__ ------------>|
  |<-- __OTB_ACK_COMMIT -----------|
  |                                |
```

On error at any point, the target responds with `__OTB_ERROR__:reason` where reason can be:
`busy`, `no_partition`, `begin_failed`, `no_session`, `aborted`, `end_failed`, `boot_failed`,
`chunk_format`, `chunk_parts`, `chunk_seq`, `chunk_order`, `chunk_decode`, `write_failed`, `timeout`.

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

In case Lizard terminates with a backtrace printed to the serial terminal, you can use the following script to print corresponding source code lines.

```bash
./backtrace.sh <addresses>
```

Note that the script assumes Espressif IDF tools being installed at `~/esp/esp-tools_4.4/` and a compiled ELF file being located at `build/lizard.elf`.

### Releasing

To build a new release, tag the commit with a "v" prefix, for example "v0.1.4".
A GitHub action will build the binary and create a new release.
After creation you can fill in a description if necessary.
