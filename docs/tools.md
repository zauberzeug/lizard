# Tools

## Installation

### Flash

To install Lizard on your ESP32 run

```bash
sudo ./espresso.py flash -d <device_path>
```

Note that flashing may require root access (hence the sudo).
The command also does not work while the serial interface is busy communicating with another process.

Use the `--reset-ota` flag to reset the OTA partition to OTA 0 after flashing:

```bash
sudo ./espresso.py flash -d <device_path> --reset-ota
```

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

### Configure

Use the configure script to send a new startup script to the microcontroller.

```bash
./configure.py <config_file> <device_path>
```

Note that the configure script cannot communicate while the serial interface is busy communicating with another process.

### Device ID Management

Use the id_manager script to configure device IDs for multi-device setups with ID-based addressing:

```bash
# Get current device ID
./id_manager.py --get -p <device_path>

# Set device ID (0-9)
./id_manager.py --set <device_id> -p <device_path>

# Get device ID from specific target (e.g., plexus expander)
./id_manager.py --get -p <device_path> --target <target_id>

# Set device ID on specific target
./id_manager.py --set <device_id> -p <device_path> --target <target_id>

# Enable external mode
./id_manager.py --enable-external -p <device_path>

# Disable external mode
./id_manager.py --disable-external -p <device_path>
```

**Important:** When setting a device ID with `--target`, the device changes what ID it responds to. For example, `--set 5 --target 0` tells the device currently responding to `$0` to change its ID to `5`. After this change, the device will only respond to `$5` commands, not `$0`.

Device IDs enable multiple ESP32 devices to share the same UART bus while responding only to commands prefixed with their specific ID (e.g., `$2core.get_device_id()`). The `--target` option allows managing IDs on specific expanders that are already in external mode. External mode enables devices to receive commands from external sources while passing through unmatched commands.

### Over-the-Air Updates

Use the OTA script to perform firmware updates via UART without physical access:

```bash
# Basic OTA to core
./lizard_ota.py build/lizard.bin -p <device_path>

# OTA to specific target (e.g., plexus expander)
./lizard_ota.py build/lizard.bin -p <device_path> --target <target_name>

# OTA to device with specific ID in multi-device setup
./lizard_ota.py build/lizard.bin -p <device_path> --target <target_name> --id <device_id>

# Debug mode for troubleshooting
./lizard_ota.py build/lizard.bin -p <device_path> --debug
```

Supported targets include `core` (default) or individual expanders like `p0`. For `Plexus` type expanders with an ID, use `--id` tag to target the correct ESP32.

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
