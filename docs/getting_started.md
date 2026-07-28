# Getting Started

## Installation

1. Download and unpack the zip file of the [latest release](https://github.com/zauberzeug/lizard/releases).
2. Install the Python requirements with `python3 -m pip install -r requirements.txt`
   (on macOS use `requirements-macos.txt`, which omits `gpiod`).
   Flashing runs under `sudo`, so install them for the root interpreter as well.
3. Attach an Espressif ESP32 microcontroller via serial to your computer.
4. Run `sudo ./espresso.py flash` to install Lizard on the ESP32.
   Add `--device /dev/<serial device name>` to pick the adapter yourself, which is required when you run non-interactively.

## Try Out

You can launch an interactive shell with `./monitor.py` to try out configurations and watch Lizard outputs (see [tools](tools.md#serial-monitor) for more details).
To verify that the communication is working, use one of the following commands to generate some output:

```
core.info()
core.millis
core.print("Hello, Lizard!")
```

See the [module reference](module_reference.md) for other commands.

To try out individual modules, you can get their current properties or unmute them for continuous output, e.g.:

```
estop = Input(34)
estop.level
estop.unmute()
```

## Wiring

Of course you should connect the ESP32 to some hardware you want to control.
From basic pins like LEDs (see [Output](module_reference.md#output)) and buttons (see [Input](module_reference.md#input))
to communication via [CAN](module_reference.md#can-interface) and control of [stepper motors](module_reference.md#stepper-motor).

## Startup Script

You can create a startup script for rules which should be directly applied after boot of the microcontroller.
Simply write the commands into a file like `on_startup.lizard` and set them with

```bash
./configure.py on_startup.lizard
```

Add the device path (e.g. `./configure.py on_startup.lizard /dev/<serial device name>`)
if several adapters are attached or you run non-interactively.

See [Tools](tools.md#configure) for more details.
