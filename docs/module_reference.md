# Module Reference

All Lizard modules have the following methods in common.

| Methods              | Description                                                             |
| -------------------- | ----------------------------------------------------------------------- |
| `module.mute()`      | Turn output off                                                         |
| `module.unmute()`    | Turn output on                                                          |
| `module.shadow()`    | Send all method calls also to another module                            |
| `module.broadcast()` | Regularly send properties to another microcontroller (for internal use) |

Shadows are useful if multiple modules should behave exactly the same, e.g. two actuators that should always move synchronously.

The `broadcast` method is used internally with [port expanders](#expander).

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
| `core.info()`         | Show lizard version and compile time          |           |
| `core.print(...)`     | Print arbitrary arguments to the command line | arbitrary |
| `core.output(format)` | Define the output format                      | `str`     |

The output `format` is a string with multiple space-separated elements of the pattern `<module>.<property>[:<precision>]`.
The `precision` is an optional integer specifying the number of decimal places for a floating point number.
For example, the format `"core.millis input.level motor.position:3"` might yield an output like `"92456 1 12.789`.

## Bluetooth

Lizard can receive messages via Bluetooth Low Energy.
Simply create a Bluetooth module with a device name of your choice.

| Constructor                          | Description                                        | Arguments |
| ------------------------------------ | -------------------------------------------------- | --------- |
| `bluetooth = Bluetooth(device_name)` | initialize bluetooth with advertised `device_name` | `str`     |

Lizard will offer a service 23014CCC-4677-4864-B4C1-8F772B373FAC and a characteristic 37107598-7030-46D3-B688-E3664C1712F0 that allows writing Lizard statements like on the command line.

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
| `input.pulloff()`  | Remove internal pull resistor      |

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

| Methods                        | Description                    | Arguments |
| ------------------------------ | ------------------------------ | --------- |
| `serial.send(b0, b1, b2, ...)` | Send a number of bytes         | `int`s    |
| `serial.read()`                | Read and output current buffer |           |

This module might be used by other modules that communicate with peripherals via serial.
You can, however, unmute the serial module to have incoming messages printed to the command line instead of keeping them buffered for other modules.

## Linear motor

This module controls a linear actuator via two output pins (move in, move out) and two input pins reading two limit switches (end in, end out).

| Constructor                                               | Description                           | Arguments |
| --------------------------------------------------------- | ------------------------------------- | --------- |
| `motor = LinearMotor(move_in, move_out, end_in, end_out)` | motor control pins and limit switches | 4x `int`  |

| Methods        | Description |
| -------------- | ----------- |
| `motor.in()`   | Move in     |
| `motor.out()`  | Move out    |
| `motor.stop()` | Stop motor  |

## ODrive Motor

The ODrive motor module controls a motor using an [ODrive motor controller](https://odriverobotics.com/).

| Constructor                        | Description            | Arguments         |
| ---------------------------------- | ---------------------- | ----------------- |
| `motor = ODriveMotor(can, can_id)` | CAN module and node ID | CAN module, `int` |

| Properties          | Description             | Data type |
| ------------------- | ----------------------- | --------- |
| `motor.position`    | Motor position (meters) | `float`   |
| `motor.tick_offset` | Encoder tick offset     | `float`   |
| `motor.m_per_tick`  | Meters per encoder tick | `float`   |
| `motor.reversed`    | Reverse motor direction | `bool`    |

| Methods               | Description                           | Arguments |
| --------------------- | ------------------------------------- | --------- |
| `motor.zero()`        | Set current position as zero position |           |
| `motor.power(torque)` | Move with given `torque`              | `float`   |
| `motor.speed(speed)`  | Move with given `speed` (m/s)         | `float`   |
| `motor.off()`         | Turn motor off (idle state)           |           |

## ODrive Wheels

The ODrive wheels module combines to ODrive motors and provides odometry and steering for differential wheeled robots.

| Constructor                                     | Description              | Arguments                |
| ----------------------------------------------- | ------------------------ | ------------------------ |
| `wheels = ODriveWheels(left_motor, left_motor)` | Two ODrive motor modules | two ODrive motor modules |

| Properties             | Description           | Data type |
| ---------------------- | --------------------- | --------- |
| `wheels.width`         | wheel distance (m)    | `float`   |
| `wheels.linear_speed`  | Forward speed (m/s)   | `float`   |
| `wheels.angular_speed` | Turning speed (rad/s) | `float`   |

| Methods                         | Description                                     | Arguments        |
| ------------------------------- | ----------------------------------------------- | ---------------- |
| `wheels.power(left, right)`     | Move with torque per wheel                      | `float`, `float` |
| `wheels.speed(linear, angular)` | Move with `linear`/`angular` speed (m/s, rad/s) | `float`, `float` |
| `wheels.off()`                  | Turn both motors off (idle state)               |                  |

## RMD Motor

The RMD motor module controls a [Gyems](http://www.gyems.cn/) RMD motor via CAN.

| Constructor                     | Description                    | Arguments         |
| ------------------------------- | ------------------------------ | ----------------- |
| `rmd = RmdMotor(can, motor_id)` | CAN module and motor ID (1..8) | CAN module, `int` |

| Properties         | Description                                    | Data type |
| ------------------ | ---------------------------------------------- | --------- |
| `rmd.position`     | Multi-turn motor position (deg)                | `float`   |
| `rmd.ratio`        | Transmission from motor to shaft (default: 6)  | `float`   |
| `rmd.torque`       | Current torque                                 | `float`   |
| `rmd.speed`        | Current speed (deg/s)                          | `float`   |
| `rmd.can_age`      | Time since last CAN message from motor (s)     | `float`   |
| `rmd.map_distance` | Distance to leading motor (deg)                | `float`   |
| `rmd.map_speed`    | Computed speed to follow leading motor (deg/s) | `float`   |

| Methods                       | Description                                               | Arguments              |
| ----------------------------- | --------------------------------------------------------- | ---------------------- |
| `rmd.power(torque)`           | Move with given `torque` (-32..32 A)                      | `float`                |
| `rmd.speed(speed)`            | Move with given `speed` (deg/s)                           | `float`                |
| `rmd.position(pos)`           | Move to and hold at `pos` (deg)                           | `float`                |
| `rmd.position(pos, speed)`    | Move to and hold at `pos` (deg) with max. `speed` (deg/s) | `float`, `float`       |
| `rmd.stop()`                  | Stop motor (but keep operating state)                     |                        |
| `rmd.resume()`                | Resume motor (continue in state from before stop command) |                        |
| `rmd.off()`                   | Turn motor off (clear operating state)                    |                        |
| `rmd.hold()`                  | Hold current position                                     |                        |
| `rmd.map(leader)`             | Map another RMD with current offset and scale 1           | RMD module             |
| `rmd.map(leader, m)`          | Map another RMD with current offset and scale `m`         | RMD module, 1x `float` |
| `rmd.map(leader, m, n)`       | Map another RMD with offset `n` and scale `m`             | RMD module, 2x `float` |
| `rmd.map(leader, a, b, c, d)` | Map another RMD from interval (a, b) to (c, d)            | RMD module, 4x `float` |
| `rmd.unmap()`                 | Stop mapping                                              |                        |
| `rmd.get_health()`            | Print temperature (C), voltage (V) and error code         |                        |
| `rmd.get_pid()`               | Print PID parameters Kp/Ki for position/speed/torque loop |                        |
| `rmd.get_acceleration()`      | Print acceleration setting                                |                        |
| `rmd.set_acceleration()`      | Set acceleration                                          | `int`                  |
| `rmd.clear_errors()`          | Clear motor error                                         |                        |
| `rmd.zero()`                  | Write position to ROM as zero position (see below)        |                        |

**The zero command**

The `zero()` method should be used with care!
In contrast to other commands it blocks the main loop for up to 200 ms and requires restarting the motor to take effect.
Furthermore, multiple writes will affect the chip life, thus it is not recommended to use it frequently.

**Mapping movement to another RMD motor**

When mapping the movement of a following motor to a leading motor, the follower uses velocity control to follow the leader.
The follower's target speed is always computed such that it catches up within one loop cycle.
When the following motor reaches its target position and the computed speed is below 1 degree per second, the follower switches to position control and holds the current position.

The mapping interval (`a`, `b`) should not be empty, because the target position of the following motor would be undefined.

Any method call (except the `map()` method) will unmap the motor.
This avoids extreme position jumps and inconsistencies caused by multiple control loops running at the same time.

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

## Expander

The expander module allows communication with another microcontroller connected via [serial](#serial-interface).

| Constructor                                 | Description                        | Arguments               |
| ------------------------------------------- | ---------------------------------- | ----------------------- |
| `expander = Expander(serial, boot, enable)` | Serial module and boot/enable pins | Serial module, 2x `int` |

| Methods                 | Description                                      | Arguments |
| ----------------------- | ------------------------------------------------ | --------- |
| `expander.run(command)` | Run any `command` on the other microcontroller   | `string`  |
| `expander.disconned()`  | Disconnect serial connection and pins            |           |
| `expander.flash()`      | Flash other microcontroller with own binary data |           |

The `disconnect()` method might be useful to access the other microcontroller on UART0 via USB while still being physically connected to the main microcontroller.

Note that the expander forwards all other method calls to the remote core module, e.g. `expander.info()`.

## Proxy

-- _This module is mainly for internal use with the expander module._ --

Proxy modules serve as handles for remote modules running on another microcontroller.
Declaring a module `x = Proxy()` will allow formulating rules like `when x.level == 0 then ...`.
It will receive property values from a remote module with the same name `x`, e.g. an input signal level.
Note that the remote module has to have turned on broadcasting: `x.broadcast()`.

| Constructor        |
| ------------------ |
| `module = Proxy()` |

Note that the proxy module forwards all method calls to the remote module.
