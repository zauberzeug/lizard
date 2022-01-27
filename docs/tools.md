# Tools

## Interaction

### Serial monitor

Use the serial monitor to read the current output and interactively send Lizard or [control commands](control_commands.md) to the microcontroller.

```bash
./monitor.py [<device_path>]
```

You can also use an SSH monitor to access a microcontroller via SSH:

```bash
./monitor_ssh.sh <user@host>
```

Note that the serial monitor cannot communicate while the serial interface is busy communicating with another process.

### Configure script

Use the configure script to send a new startup script to the microcontroller.

```bash
./configure.py <config_file> <device_path>
```

Note that the configure script cannot communicate while the serial interface is busy communicating with another process.

## Development

### Get OWL

In order to make changes to the Lizard language file, you need to install OWL.
The following script clones the current version from [Github](https://github.com/ianh/owl).

```bash
./get_owl.sh
```

### Compile script

After making changes to the Lizard language definition or its C++ implementation, you can use the compile script to generate a new parser and executing the compilation in an Espressif IDF Docker container.

```bash
./compile.sh
```

### Upload new firmware

To upload the compiled firmware you can use one of the following scripts.

#### Upload via USB

```bash
./upload_usb.sh [<device_path>]
```

#### Upload via SSH

```bash
./upload_ssh.sh <user@host>
```

#### Upload via ttyTHS1

```bash
./upload_ths1.sh
```

This script is useful on [Robot Brains](https://www.zauberzeug.com/product-robot-brain.html) where the microcontroller is connected to the pin header of an NVIDIA Jetson computer.

#### Upload to an expander

```bash
./upload_expander.sh
```

This script is still work in progress.
It is useful on [Robot Brains](https://www.zauberzeug.com/product-robot-brain.html) that come with a second microcontroller as port expander.

### Backtrace

In case Lizard terminates with a backtrace printed to the serial terminal, you can use the following script to print corresponding source code lines.

```bash
./backtrace.sh <addresses>
```

Note that the script assumes Espressif IDF tools being installed at `~/esp/esp-tools_4.2/` and a compiled ELF file being located at `build/lizard.elf`.
