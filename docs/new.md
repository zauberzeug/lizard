# The Lizard Language

## Statements

Statements are input via the command-line interface or stored in the startup script on the microcontroller.
The following statement types are currently supported.

**Expressions**

An expression can be a constant value, a variable, a module property, an arithmetic or logical expression as well as various combinations:

    true
    42
    3.14
    "Hello world"
    led
    button.level
    1 + (2 - 3 * 4)**5
    1 == 2 or (x == 4 and button.level == 0)

Expressions can be assigned to variables, used in conditions and passed to constructors, method calls or routine calls.
Plain expression statements print their result to the command line.

**Variables: declaration and assignment**

New variables need to be explicitly declared with a data type:

    int i

They can be immediately initialized:

    int i = 1

Otherwise they have an initial value of `false`, `0`, `0.0` or `""`, respectively.

Variables can be assigned a new value of compatible data type:

    i = 2

**Modules: constructors, method calls and property assignments**

Constructors are used to create module instances:

    led = Output(15)

See the module reference for more details about individual modules and their argument lists.

You can call module methods as follows:

    led.on()

Some module properties are meant to be written to:

    motor.ratio = 9

**Routines: definition and call**

Routines have a name and contain a list of actions:

    all_on := green.on(); red.on(); end

They can be called similar to module methods:

    all_on()

**Rules: definition**

Rules execute a list of actions when a condition is met:

    when button.level == 0 then
        core.print("Off!")
        led.off()
    end

In contrast to an if-statement known from other languages a when-condition is checked in every cycle of Lizard's main loop.
Whenever the condition is true, the actions are executed.

Note that actions can be asynchronous.
If there are still actions running asynchronously, a truthy condition is ignored.

## Actions

Routines and rules contain a list of actions.

**Method and routine calls**

Like with a method call statement, you can call methods and routines with an action:

    when clicked then
        core.print("On!")
        all_on()
    end

**Property and variable assignments**

Like with the corresponding assignment statements, you can assign properties and variables with an action as well:

    when i > 0 then
        i = 0
        core.debug = true
    end

**Await conditions and routines**

In contrast to statements, actions can _await_ conditions, causing the execution of subsequent actions to wait until the condition is met.

    int t
    blink :=
        t = core.millis
        led.on()
        await core.millis > t + 1000
        led.off()
    end

Similarly, actions can await asynchronous routines, causing subsequent actions to wait until a routine is completed.

    when button.level == 0 then
        core.print("Blink...")
        await blink()
        core.print("Done.")
    end

## Data types

Lizard currently supports five data types:

| Data type             | Example            | Range                   |
| --------------------- | ------------------ | ----------------------- |
| boolean               | `bool b = true`    | `false`, `true`         |
| integer number        | `int i = 0`        | 64-bit unsigned integer |
| floating point number | `float f = 0.0`    | 64-bit float            |
| string                | `str s = "foo"`    |
| identifier            | `led = Output(15)` |

Note that identifiers cannot be created via variable declarations, but only via constructors.

Implicit conversion only happens from integers to floating point numbers:

    int i = 42
    float f = i + 3.14

## Whitespace, comments and line breaks

Tabs and spaces are treated as whitespace.

Blank lines are interpreted as no-op and do nothing.

Line comments start with `//`.

Multiple statements or actions are separated with `;` or a newline.

# Machine Safety

Lizard implements the following features to increase machine safety.

## Checksums

Each line sent via the command-line interface can and should be followed by a checksum.
Lizard will omit any lines with incorrect checksums.
Any output is as well sent with a checksum.

The 8-bit checksum is computed as the bitwise XOR of all characters excluding the newline character and written as a two-digit hex number (with leading zeros) separated with an `@` character, for example:

| Line    | Bitwise XOR                             | Result     |
| ------- | --------------------------------------- | ---------- |
| `1 + 2` | 0x31 ^ 0x20 ^ 0x2b ^ 0x20 ^ 0x32 = 0x28 | `1 + 2@28` |

## Keep-alive signal

TODO

# Control commands

Lines with a leading `!` can indicate one of the following control commands.

| Command | Meaning                                                  |
| ------- | -------------------------------------------------------- |
| `!+abc` | Add `abc` to the startup script                          |
| `!-abc` | Remove lines starting with `abc` from the startup script |
| `!?`    | Print the startup script                                 |
| `!.`    | Write the startup script to non-volatile storage         |
| `!!abc` | Interpret `abc` as Lizard code                           |
| `!"abc` | Print `abc` to the command-line                          |
| `!>abc` | Send `abc` via UART1 to another microcontroller          |

Note that the commands `!+`, `!-` and `!?` affect the startup script in RAM, which is only written to non-volatile storage with the `!.` command.

Input from the default command-line interface UART0 is usually interpreted as Lizard code;
input from another microcontroller connected on UART1 is usually printed to the command-line on UART0.
This behavior can be changed using `!!`, `!"` and `!>`.

# Tools

## Serial monitor

```bash
monitor.py [<device_path>]
```

## Configure script

```bash
configure.py <config_file> <device_path>
```

# Module Reference

All Lizard modules have the following methods in common.

| Methods              | Description                                          |
| -------------------- | ---------------------------------------------------- |
| `module.mute()`      | Turn output off                                      |
| `module.unmute()`    | Turn output on                                       |
| `module.broadcast()` | Regularly send properties to another microcontroller |
| `module.shadow()`    | Send all method calls also to another module         |

Broadcasting allows connecting modules across multiple microcontrollers.

Shadows are useful if multiple modules should behave exactly the same, e.g. two actuators that should always move synchronously.

## Core

The core module encapsulates various properties and methods that are related to the microcontroller itself.
It is automatically created right after the boot sequence.

| Properties    | Description                                             | Data type |
| ------------- | ------------------------------------------------------- | --------- |
| `core.debug`  | Whether to output debug information to the command line | `bool`    |
| `core.millis` | Time since booting the microcontroller (ms)             | `int`     |
| `core.heap`   | Free heap memory (bytes)                                | `int`     |

| Methods               | Description                                   | Arguments |
| --------------------- | --------------------------------------------- | --------- |
| `core.restart()`      | Restart the microcontroller                   |           |
| `core.print(...)`     | Print arbitrary arguments to the command line | arbitrary |
| `core.output(format)` | Define the output format                      | `str`     |

The output `format` is a string with multiple space-separated elements of the pattern `<module>.<property>[:<precision>]`.
The `precision` is an optional integer specifying the number of decimal places for a floating point number.
For example, the format `"core.millis input.level motor.position:3"` might yield an output like `"92456 1 12.789`.

## Input

The input module is associated with a digital input pin that is be connected to a pushbutton, sensor or other input signal.

| Constructor          | Description                            | Arguments |
| -------------------- | -------------------------------------- | --------- |
| `input = Input(pin)` | `pin` is the corresponding GPIO number | `int`     |

| Properties     | Description                           | Data type |
| -------------- | ------------------------------------- | --------- |
| `input.level`  | Current signal level (0 or 1)         | `int`     |
| `input.change` | Level change since last cycle (-1..1) | `int`     |

| Methods            | Description                        |
| ------------------ | ---------------------------------- |
| `input.get()`      | Output the current level           |
| `input.pullup()`   | Add an internal pull-up resistor   |
| `input.pulldown()` | Add an internal pull-down resistor |

## Output

The output module is associated with a digital output pin that is connected to an LED, actuator or other output signal.

| Constructor            | Description                            | Arguments |
| ---------------------- | -------------------------------------- | --------- |
| `output = Output(pin)` | `pin` is the corresponding GPIO number | `int`     |

| Methods        | Description             |
| -------------- | ----------------------- |
| `output.on()`  | Set the output pin high |
| `output.off()` | Set the output pin low  |

## CAN interface

The CAN module allows communicating with peripherals on the specified CAN bus.

| Constructor               | Description              | Arguments           |
| ------------------------- | ------------------------ | ------------------- |
| `can = Can(rx, tx, baud)` | RX/TX pins and baud rate | `int`, `int`, `int` |

| Methods                                             | Description                    | Arguments |
| --------------------------------------------------- | ------------------------------ | --------- |
| `can.send(node_id, d0, d1, d2, d3, d4, d5, d6, d7)` | Send a frame with 8 data bytes | 9x `int`  |

## Serial interface

The serial module allows communicating with peripherals via the specified connection.

| Constructor                          | Description                        | Arguments |
| ------------------------------------ | ---------------------------------- | --------- |
| `serial = Serial(rx, tx, baud, num)` | RX/TX pins, baud rate, UART number | 4x `int`  |

## Odrive Motor

The Odrive motor module controls a motor using an [Odrive motor controller](https://odriverobotics.com/).

| Constructor                        | Description            | Arguments         |
| ---------------------------------- | ---------------------- | ----------------- |
| `motor = ODriveMotor(can, can_id)` | CAN module and node ID | CAN module, `int` |

| Properties          | Description             | Data type |
| ------------------- | ----------------------- | --------- |
| `motor.position`    | Motor position (meters) | `float`   |
| `motor.tick_offset` | Encoder tick offset     | `float`   |
| `motor.m_per_tick`  | Meters per encoder tick | `float`   |

| Methods               | Description                           | Arguments |
| --------------------- | ------------------------------------- | --------- |
| `motor.zero()`        | Set current position as zero position |           |
| `motor.power(torque)` | Move with given `torque`              | `float`   |
| `motor.speed(speed)`  | Move with given `speed` (m/s)         | `float`   |
| `motor.off()`         | Turn motor off (idle state)           |           |

## RMD Motor

The RMD motor module controls a [Gyems](http://www.gyems.cn/) RMD motor via CAN.

| Constructor                     | Description                    | Arguments         |
| ------------------------------- | ------------------------------ | ----------------- |
| `rmd = RmdMotor(can, motor_id)` | CAN module and motor ID (1..8) | CAN module, `int` |

| Properties     | Description                                   | Data type |
| -------------- | --------------------------------------------- | --------- |
| `rmd.position` | Multi-turn motor position (deg)               | `float`   |
| `rmd.ratio`    | Transmission from motor to shaft (default: 6) | `float`   |
| `rmd.torque`   | Current torque                                | `float`   |
| `rmd.speed`    | Current speed                                 | `float`   |

| Methods                    | Description                                               | Arguments        |
| -------------------------- | --------------------------------------------------------- | ---------------- |
| `rmd.power(torque)`        | Move with given `torque` (-32..32 A)                      | `float`          |
| `rmd.speed(speed)`         | Move with given `speed` (deg/s)                           | `float`          |
| `rmd.position(pos)`        | Move to and hold at `pos` (deg)                           | `float`          |
| `rmd.position(pos, speed)` | Move to and hold at `pos` (deg) with max. `speed` (deg/s) | `float`, `float` |
| `rmd.stop()`               | Stop motor (but keep operating state)                     |                  |
| `rmd.resume()`             | Resume motor (continue in state from before stop command) |                  |
| `rmd.off()`                | Turn motor off (clear operating state)                    |                  |
| `rmd.hold()`               | Hold current position                                     |                  |
| `rmd.follow(leader)`       | Follow the position of another RMD motor                  | RMD module       |
| `rmd.get_health()`         | Print temperature (C), voltage (V) and error code         |                  |
| `rmd.get_pid()`            | Print PID parameters Kp/Ki for position/speed/torque loop |                  |
| `rmd.get_acceleration()`   | Print acceleration setting                                |                  |
| `rmd.set_acceleration()`   | Set acceleration                                          | `int`            |
| `rmd.clear_errors()`       | Clear motor error                                         |                  |
| `rmd.zero()`               | Write position to ROM as zero position (see below)        |                  |

Note that the `zero()` method should be used with care!
In contrast to other commands it blocks the main loop for up to 200 ms and requires restarting the motor to take effect.
Furthermore, multiple writes will affect the chip life, thus it is not recommended to use it frequently.

## RoboClaw

The RoboClaw module serves as building block for more complex modules like RoboClaw motors.
It communicates with a [Basicmicro](https://www.basicmicro.com/) RoboClaw motor driver via serial.

| Constructor                        | Description               | Arguments            |
| ---------------------------------- | ------------------------- | -------------------- |
| `claw = RoboClaw(serial, address)` | Serial module and address | Serial module, `int` |

## RoboClaw Motor

The RoboClaw motor module controls a motor using a RoboClaw module.

| Constructor                             | Description                         | Arguments              |
| --------------------------------------- | ----------------------------------- | ---------------------- |
| `motor = RoboClawMotor(claw, motor_id)` | RoboClaw module and motor ID (1..2) | RoboClaw module, `int` |

| Properties       | Description                               | Data type |
| ---------------- | ----------------------------------------- | --------- |
| `motor.position` | Multi-turn motor position (encoder ticks) | `int`     |

| Methods               | Description                             | Arguments |
| --------------------- | --------------------------------------- | --------- |
| `motor.power(torque)` | Move with given `torque` (-1..1)        | `float`   |
| `motor.speed(speed)`  | Move with given `speed` (-32767..32767) | `float`   |
| `motor.zero()`        | Store position as zero position         |           |

## Proxy

Proxy modules serve as handles for remote modules running on another microcontroller.
Declaring a module `x = Proxy()` will allow formulating rules like `when x.level == 0 then ...`.
It will receive property values from a remote module with the same name `x`, e.g. an input signal level.
Note that the remote module has to have turned on broadcasting: `x.broadcast()`.

| Constructor        |
| ------------------ |
| `module = Proxy()` |
