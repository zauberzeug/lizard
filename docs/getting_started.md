# Getting Started

## Installation

1. Download and unpack the zip file of the [latest release](https://github.com/zauberzeug/lizard/releases).
2. attach an espressif ESP32 micro controller via serial to your computer
3. run `sudo ./flash.py /dev/<serial dev name>` to install Lizard on the esp32

## Try Out

You launch an interactive shell with `./monitor.py` to try out configurations (see [Tools](tools.md#serial-monitor) for more details).
To verify that the communication is working, use one of the following commands to generate some output:

    core.info()
    core.millis
    core.print("Hello, Lizard!")

See the [module reference](module_reference.md) for other commands.

To try out individual modules, you can get their current properties or unmute them for continuous output, e.g.:

    estop = Input(34)
    estop.level
    estop.unmute()

## Wireing

Of course you should connect the ESP32 to some hardware you want to control.
From basic pins like LEDs (see [Output](module_reference.md#output)) and buttons (see [Input](module_reference.md#input))
to communication via [CAN](module_reference.md#can-interface) and control of [stepper motors](module_reference.md#stepper-motor).
