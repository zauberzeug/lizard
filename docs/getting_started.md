# Getting Started

You launch an interactive shell with `./monitor.py` to try out configurations.
To verify that the communication is working, use one of the following commands to generate some output:

    core.info()
    core.millis
    core.print("Hello, Lizard!")

See the [module reference](module_reference.md) for other commands.

To try out individual modules, you can get their current properties or unmute them for continuous output, e.g.:

    estop = Input(34)
    estop.level
    estop.unmute()
