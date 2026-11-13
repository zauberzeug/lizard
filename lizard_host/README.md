# lizard-host

Host-side tools and library for microcontrollers running [Lizard](https://lizard.dev),
the domain-specific language for robot hardware by [Zauberzeug](https://zauberzeug.com).

```bash
pip install lizard-host
```

## Tools

- `lizard-monitor` shows the output of a microcontroller, decodes telemetry frames and sends the commands you type.
- `lizard-configure` writes a startup script to a microcontroller, directly or as a peer of a serial bus.
- `lizard-espresso` flashes, erases, enables, disables and resets a microcontroller, also on a Robot Brain via SSH.
- `lizard-otb-update` pushes firmware to a serial bus peer through its coordinator.

## Library

```python
import serial
import lizard_host as lh

with serial.Serial('/dev/ttyUSB0', 115200, timeout=0.1) as port:
    connection = lh.Connection(port)
    print(connection.read_version())
    connection.write_startup(open('robot.liz').read())
```

The package also decodes telemetry frames, merges them into steps, parses `core` lines and keeps the clock offset,
and runs the same procedures in an async host with `AsyncConnection`.

The version of the package is the Lizard release it belongs to; it works with firmware of that release and older,
and refuses a startup script that uses a feature the firmware lacks.
A peer of a serial bus with another name than `bus` needs a coordinator of Lizard 0.16.0 or newer.
See the [documentation](https://lizard.dev/tools/) for details.
