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

| Methods                          | Description                                        | Arguments    |
| -------------------------------- | -------------------------------------------------- | ------------ |
| `core.restart()`                 | Restart the microcontroller                        |              |
| `core.version()`                 | Show lizard version                                |              |
| `core.info()`                    | Show lizard version, compile time and IDF version  |              |
| `core.print(...)`                | Print arbitrary arguments to the command line      | arbitrary    |
| `core.output(format)`            | Define the output format                           | `str`        |
| `core.startup_checksum()`        | Show 16-bit checksum of the startup script         |              |
| `core.ota(ssid, password, url)`  | Starts OTA update on a URL with given WiFi         | 3x `str`     |
| `core.get_pin_status(pin)`       | Print the status of the chosen pin                 | `int`        |
| `core.set_pin_level(pin, value)` | Turns the pin into an output and sets its level    | `int`, `int` |
| `core.get_pin_strapping(pin)`    | Print value of the pin from the strapping register | `int`        |

The output `format` is a string with multiple space-separated elements of the pattern `<module>.<property>[:<precision>]` or `<variable>[:<precision>]`.
The `precision` is an optional integer specifying the number of decimal places for a floating point number.
For example, the format `"core.millis input.level motor.position:3"` might yield an output like `"92456 1 12.789`.

The OTA update will try to connect to the specified WiFi network with the provided SSID and password.
After initializing the WiFi connection, it will attempt an OTA update from the given URL.
Upon successful updating, the ESP will restart and attempt to verify the OTA update.
It will reconnect to the WiFi and try to access URL + `/verify` to receive a message with the current version of Lizard.
The test is considered successful if an HTTP request is received, even if the version does not match or is empty.
If the newly updated Lizard cannot connect to URL + `/verify`, the OTA update will be rolled back.

`core.get_pin_status(pin)` reads the pin's voltage, not the output state directly.

## Bluetooth

Lizard can receive messages via Bluetooth Low Energy, and also send messages in return to a connected device.
Simply create a Bluetooth module with a device name of your choice.

| Constructor                          | Description                                        | Arguments |
| ------------------------------------ | -------------------------------------------------- | --------- |
| `bluetooth = Bluetooth(device_name)` | initialize bluetooth with advertised `device_name` | `str`     |
| `bluetooth.send(data)`               | send `data` via notification                       | `str`     |

Lizard will offer a service 23014CCC-4677-4864-B4C1-8F772B373FAC and a characteristic 37107598-7030-46D3-B688-E3664C1712F0 that allows writing Lizard statements like on the command line. On a second characteristic 19f91f52-e3b1-4809-9d71-bc16ecd81069 notifications will be emitted when `send(data)` is executed.

## Input

The input module is associated with a digital input pin that is be connected to a pushbutton, sensor or other input signal.

| Constructor          | Description                            | Arguments |
| -------------------- | -------------------------------------- | --------- |
| `input = Input(pin)` | `pin` is the corresponding GPIO number | `int`     |

| Properties       | Description                           | Data type |
| ---------------- | ------------------------------------- | --------- |
| `input.level`    | Current signal level (0 or 1)         | `int`     |
| `input.change`   | Level change since last cycle (-1..1) | `int`     |
| `input.inverted` | Inverts the active property if true   | `bool`    |
| `input.active`   | Current active state of the input     | `bool`    |

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

| Properties      | Description                           | Data type |
| --------------- | ------------------------------------- | --------- |
| `output.level`  | Current signal level (0 or 1)         | `int`     |
| `output.change` | Level change since last cycle (-1..1) | `int`     |

| Methods                                | Description                               | Arguments |
| -------------------------------------- | ----------------------------------------- | --------- |
| `output.on()`                          | Set the output pin high                   |           |
| `output.off()`                         | Set the output pin low                    |           |
| `output.level(value)`                  | Set the output level to the given `value` | `bool`    |
| `output.pulse(interval[, duty_cycle])` | Switch output on and off                  | `float`s  |

The `pulse()` method allows pulsing an output with a given interval in seconds and an optional duty cycle between 0 and 1 (0.5 by default).
Note that the pulsing frequency is limited by the main loop to around 20 Hz.

## PWM Output

The PWM output module is associated with a digital output pin that is connected to an LED, actuator or other output signal.

| Constructor            | Description                            | Arguments |
| ---------------------- | -------------------------------------- | --------- |
| `pwm = PwmOutput(pin)` | `pin` is the corresponding GPIO number | `int`     |

| Properties         | Description                              | Data type |
| ------------------ | ---------------------------------------- | --------- |
| `output.duty`      | Duty cycle (8 bit: 0..256, default: 128) | `int`     |
| `output.frequency` | Frequency (Hz, default: 1000)            | `int`     |

| Methods        | Description             | Arguments |
| -------------- | ----------------------- | --------- |
| `output.on()`  | Turn on the PWM signal  |           |
| `output.off()` | Turn off the PWM signal |           |

## MCP23017 Port Expander

The MCP23017 allows controlling up to 16 general purpose input or output pins via I2C.

| Constructor                                                    | Description | Arguments |
| -------------------------------------------------------------- | ----------- | --------- |
| `mcp = Mcp23017([port[, sda[, scl[, address[, clk_speed]]]]])` | See below   | `int`s    |

The constructor expects up to five arguments:

- `port`: 0 or 1, since the ESP32 has two I2C ports (default: 0)
- `sda`: SDA pin (default: 21)
- `scl`: SCL pin (default: 22)
- `address`: client address of the MCP (0x20..0x28, default: 0x20)
- `clk_speed`: I2C clock speed (default: 100000)

| Properties    | Description                       | Data type |
| ------------- | --------------------------------- | --------- |
| `mcp.levels`  | Levels of all 16 pins             | `int`     |
| `mcp.inputs`  | Input mode of all 16 pins         | `int`     |
| `mcp.pullups` | Pull-up resistors for all 16 pins | `int`     |

The properties `levels`, `inputs` and `pullups` contain binary information for all 16 pins in form of a 16 bit unsigned integer.

| Methods              | Description                           | Arguments |
| -------------------- | ------------------------------------- | --------- |
| `mcp.levels(value)`  | Set levels of all 16 pins             | `int`     |
| `mcp.inputs(value)`  | Set input mode of all 16 pins         | `int`     |
| `mcp.pullups(value)` | Set pull-up resistors for all 16 pins | `int`     |

The methods `levels()`, `inputs()` and `pullups()` expect a 16 bit unsigned integer `value` containing binary information for all 16 pins.

Use `inputs()` to configure input and output pins, e.g. `inputs(0xffff)` all inputs or `inputs(0x0000)` all outputs.
While `levels()` will only affect output pins, `pullups()` will only affect the levels of input pins.

Using an MCP23017 port expander module you can not only access individual pins.
You can also instantiate the following modules passing the `mcp` instance as the first argument:

- Input: `input = Input(mcp, pin)`
- Output: `output = Output(mcp, pin)`
- Linear motor: `motor = LinearMotor(mcp, move_in, move_out, end_in, end_out)`

The pins `pin`, `move_in`, `move_out`, `end_in` and `end_out` are numbers from 0 to 15 referring to A0...A7 and B0...B7 on the MCP23017.

## IMU

The IMU module provides access to a Bosch BNO055 9-axis absolute orientation sensor.
Currently, only reading the accelerometer is implemented.

| Constructor                                               | Description | Arguments |
| --------------------------------------------------------- | ----------- | --------- |
| `imu = Imu([port[, sda[, scl[, address[, clk_speed]]]]])` | See below   | `int`s    |

The constructor expects up to five arguments:

- `port`: 0 or 1, since the ESP32 has two I2C ports (default: 0)
- `sda`: SDA pin (default: 21)
- `scl`: SCL pin (default: 22)
- `address`: client address of the MCP (0x28 or 0x29, default: 0x28)
- `clk_speed`: I2C clock speed (default: 100000)

| Properties    | Description                           | Data type |
| ------------- | ------------------------------------- | --------- |
| `imu.acc_x`   | acceleration in x direction (m/s^2)   | `float`   |
| `imu.acc_y`   | acceleration in y direction (m/s^2)   | `float`   |
| `imu.acc_z`   | acceleration in z direction (m/s^2)   | `float`   |
| `imu.roll`    | roll (degrees, see datasheet)         | `float`   |
| `imu.pitch`   | pitch (degrees, see datasheet)        | `float`   |
| `imu.yaw`     | yaw (degrees, see datasheet)          | `float`   |
| `imu.quat_w`  | quaternion component w                | `float`   |
| `imu.quat_x`  | quaternion component x                | `float`   |
| `imu.quat_y`  | quaternion component y                | `float`   |
| `imu.quat_z`  | quaternion component z                | `float`   |
| `imu.cal_sys` | calibration of system (0 to 3)        | `float`   |
| `imu.cal_gyr` | calibration of gyroscope (0 to 3)     | `float`   |
| `imu.cal_acc` | calibration of accelerometer (0 to 3) | `float`   |
| `imu.cal_mag` | calibration of magnetometer (0 to 3)  | `float`   |

## CAN interface

The CAN module allows communicating with peripherals on the specified CAN bus.

| Constructor               | Description              | Arguments           |
| ------------------------- | ------------------------ | ------------------- |
| `can = Can(rx, tx, baud)` | RX/TX pins and baud rate | `int`, `int`, `int` |

| Methods                                             | Description                    | Arguments |
| --------------------------------------------------- | ------------------------------ | --------- |
| `can.send(node_id, d0, d1, d2, d3, d4, d5, d6, d7)` | Send a frame with 8 data bytes | 9x `int`  |
| `can.get_status()`                                  | Print the driver status        |           |
| `can.start()`                                       | Start the driver               |           |
| `can.stop()`                                        | Stop the driver                |           |
| `can.recover()`                                     | Recover the driver             |           |

The method `get_status()` prints the following information:

- `state` ("STOPPED", "RUNNING", "BUS_OFF" or "RECOVERING"),
- `msgs_to_tx`,
- `msgs_to_rx`,
- `tx_error_counter`,
- `rx_error_counter`,
- `tx_failed_count`,
- `rx_missed_count`,
- `rx_overrun_count`,
- `arb_lost_count` and
- `bus_error_count`.

After creating a CAN module, the driver is started automatically.
The `start()` and `stop()` methods are primarily for debugging purposes.

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

| Properties  | Description                | Data type |
| ----------- | -------------------------- | --------- |
| `motor.in`  | Motor is in "in" position  | `bool`    |
| `motor.out` | Motor is in "out" position | `bool`    |

| Methods        | Description |
| -------------- | ----------- |
| `motor.in()`   | Move in     |
| `motor.out()`  | Move out    |
| `motor.stop()` | Stop motor  |

## ODrive Motor

The ODrive motor module controls a motor using an [ODrive motor controller](https://odriverobotics.com/).

| Constructor                                   | Description                     | Arguments                |
| --------------------------------------------- | ------------------------------- | ------------------------ |
| `motor = ODriveMotor(can, can_id[, version])` | CAN module, node ID and version | CAN module, `int`, `int` |

The `version` parameter is an optional integer indicating the patch number of the ODrive firmware (4, 5 or 6; default: 4 for version "0.5.4"). Version 0.5.6 allows to read the motor error flag.

| Properties          | Description                               | Data type |
| ------------------- | ----------------------------------------- | --------- |
| `motor.position`    | Motor position (meters)                   | `float`   |
| `motor.tick_offset` | Encoder tick offset                       | `float`   |
| `motor.m_per_tick`  | Meters per encoder tick                   | `float`   |
| `motor.reversed`    | Reverse motor direction                   | `bool`    |
| `motor.axis_state`  | State of the motor axis                   | `int`     |
| `motor.axis_error`  | Error code of the axis                    | `int`     |
| `motor.motor_error` | Motor error flat (requires version 0.5.6) | `int`     |
| `motor.speed`       | Current speed (m/s)                       | `float`   |

| Methods                        | Description                            | Arguments        |
| ------------------------------ | -------------------------------------- | ---------------- |
| `motor.zero()`                 | Set current position as zero position  |                  |
| `motor.power(torque)`          | Move with given `torque`               | `float`          |
| `motor.speed(speed)`           | Move with given `speed` (m/s)          | `float`          |
| `motor.position(position)`     | Move to given `position` (m)           | `float`          |
| `motor.limits(speed, current)` | Set speed (m/s) and current (A) limits | `float`, `float` |
| `motor.off()`                  | Turn motor off (idle state)            |                  |
| `motor.reset_motor()`          | Resets the motor and clears errors     |                  |

## ODrive Wheels

The ODrive wheels module combines to ODrive motors and provides odometry and steering for differential wheeled robots.

| Constructor                                     | Description              | Arguments                |
| ----------------------------------------------- | ------------------------ | ------------------------ |
| `wheels = ODriveWheels(left_motor, left_motor)` | Two ODrive motor modules | two ODrive motor modules |

| Properties             | Description                      | Data type |
| ---------------------- | -------------------------------- | --------- |
| `wheels.width`         | Wheel distance (m)               | `float`   |
| `wheels.linear_speed`  | Forward speed (m/s)              | `float`   |
| `wheels.angular_speed` | Turning speed (rad/s)            | `float`   |
| `wheels.enabled`       | Whether motors react to commands | `bool`    |

| Methods                         | Description                                     | Arguments        |
| ------------------------------- | ----------------------------------------------- | ---------------- |
| `wheels.power(left, right)`     | Move with torque per wheel                      | `float`, `float` |
| `wheels.speed(linear, angular)` | Move with `linear`/`angular` speed (m/s, rad/s) | `float`, `float` |
| `wheels.off()`                  | Turn both motors off (idle state)               |                  |

When the wheels are not `enabled`, `power` and `speed` method calls are ignored.
This allows disabling the wheels permanently by setting `enabled = false` in conjunction with calling the `off()` method.
Now the vehicle can be pushed manually with motors turned off, without taking care of every line of code potentially re-activating the motors.

## RMD Motor

The RMD motor module controls a [MyActuator](https://www.myactuator.com/) RMD motor via CAN.

| Constructor                            | Description                                        | Arguments                |
| -------------------------------------- | -------------------------------------------------- | ------------------------ |
| `rmd = RmdMotor(can, motor_id, ratio)` | CAN module, motor ID (1..8) and transmission ratio | CAN module, `int`, `int` |

| Properties        | Description                                | Data type |
| ----------------- | ------------------------------------------ | --------- |
| `rmd.position`    | Multi-turn motor position (deg)            | `float`   |
| `rmd.torque`      | Current torque                             | `float`   |
| `rmd.speed`       | Current speed (deg/s)                      | `float`   |
| `rmd.temperature` | Current temperature (˚C)                   | `float`   |
| `rmd.can_age`     | Time since last CAN message from motor (s) | `float`   |

| Methods                     | Description                                                       | Arguments        |
| --------------------------- | ----------------------------------------------------------------- | ---------------- |
| `rmd.power(torque)`         | Move with given `torque` (-32..32 A)                              | `float`          |
| `rmd.speed(speed)`          | Move with given `speed` (deg/s)                                   | `float`          |
| `rmd.position(pos)`         | Move to and hold at `pos` (deg)                                   | `float`          |
| `rmd.position(pos, speed)`  | Move to and hold at `pos` (deg) with max. `speed` (deg/s)         | `float`, `float` |
| `rmd.stop()`                | Stop motor (but keep operating state)                             |                  |
| `rmd.off()`                 | Turn motor off (clear operating state)                            |                  |
| `rmd.hold()`                | Hold current position                                             |                  |
| `rmd.get_pid()`             | Print PID parameters Kp/Ki for position/speed/torque loop         |                  |
| `rmd.set_pid(...)`          | Set PID parameters Kp/Ki for position/speed/torque loop           | 6x `int`         |
| `rmd.get_acceleration()`    | Print acceleration (deg/s^2)                                      |                  |
| `rmd.set_acceleration(...)` | Set accelerations/decelerations for position/speed loop (deg/s^2) | 4x `int`         |
| `rmd.get_status()`          | Print temperature [˚C], voltage [V] and motor error code          |                  |
| `rmd.clear_errors()`        | Clear motor error                                                 |                  |

**Set acceleration**

Although `get_acceleration()` prints only one acceleration per motor, `set_acceleration` distinguishes the following four parameters:

1. acceleration for position mode
2. deceleration for position mode
3. acceleration for speed mode
4. deceleration for speed mode

You can pass `0` to skip parameters, i.e. to keep individual acceleration values unchanged.

## RMD Motor Pair

The RMD motor pair module allows to synchronize two RMD motors.

| Constructor                 | Description           | Arguments           |
| --------------------------- | --------------------- | ------------------- |
| `rmd = RmdPair(rmd1, rmd2)` | Two RMD motor modules | 2x RMD Motor module |

| Properties  | Description                                   | Data type |
| ----------- | --------------------------------------------- | --------- |
| `rmd.v_max` | Maximum speed (deg/s, default: 360)           | `float`   |
| `rmd.a_max` | Maximum acceleration (deg/s² (default: 10000) | `float`   |

| Methods              | Description                             | Arguments  |
| -------------------- | --------------------------------------- | ---------- |
| `rmd.move(x, y)`     | Move motor 1 to `x` and motor 2 to `x`  | 2x `float` |
| `rmd.stop()`         | Stop motors (but keep operating state)  |            |
| `rmd.off()`          | Turn motors off (clear operating state) |            |
| `rmd.hold()`         | Hold current positions                  |            |
| `rmd.clear_errors()` | Clear motor errors                      |            |

## RoboClaw

The RoboClaw module serves as building block for more complex modules like RoboClaw motors.
It communicates with a [Basicmicro](https://www.basicmicro.com/) RoboClaw motor driver via serial.

| Constructor                        | Description               | Arguments            |
| ---------------------------------- | ------------------------- | -------------------- |
| `claw = RoboClaw(serial, address)` | Serial module and address | Serial module, `int` |

| Properties         | Description                         | Data type |
| ------------------ | ----------------------------------- | --------- |
| `claw.temperature` | Board temperature (degrees Celsius) | `float`   |

The temperature property is updated every 1 second.

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

## RoboClaw Wheels

The RoboClaw wheels module combines two RoboClaw motors and provides odometry and steering for differential wheeled robots.

| Constructor                                       | Description           | Arguments                  |
| ------------------------------------------------- | --------------------- | -------------------------- |
| `wheels = RoboClawWheels(left_motor, left_motor)` | left and right motors | two RoboClaw motor modules |

| Properties             | Description                      | Data type |
| ---------------------- | -------------------------------- | --------- |
| `wheels.width`         | Wheel distance (m)               | `float`   |
| `wheels.linear_speed`  | Forward speed (m/s)              | `float`   |
| `wheels.angular_speed` | Turning speed (rad/s)            | `float`   |
| `wheels.m_per_tick`    | Meters per encoder tick          | `float`   |
| `wheels.enabled`       | Whether motors react to commands | `bool`    |

| Methods                         | Description                                     | Arguments        |
| ------------------------------- | ----------------------------------------------- | ---------------- |
| `wheels.power(left, right)`     | Move with torque per wheel (-1..1)              | `float`, `float` |
| `wheels.speed(linear, angular)` | Move with `linear`/`angular` speed (m/s, rad/s) | `float`, `float` |
| `wheels.off()`                  | Turn both motors off (idle state)               |                  |

When the wheels are not `enabled`, `power` and `speed` method calls are ignored.

## Stepper Motor

The stepper motor module controls a stepper motor via "step" and "direction" pins.
It uses the ESP LED Control API to generate pulses with sufficiently high frequencies and the Pulse Counter API to count steps.

| Constructor                                               | Description             | Arguments |
| --------------------------------------------------------- | ----------------------- | --------- |
| `motor = StepperMotor(step, dir[, pu[, cp[, lt[, lc]]]])` | Step and direction pins | 6x `int`  |

The constructor arguments `pu` (pulse counter unit), `pc` (pulse counter channel), `lt` (LED timer) and `lc` (LED channel) are optional and default to 0.
When using multiple stepper motors, they can be set to different values to avoid conflicts.

| Properties       | Description                    | Data type |
| ---------------- | ------------------------------ | --------- |
| `motor.position` | Motor position (steps)         | `int`     |
| `motor.speed`    | Motor speed (steps per second) | `int`     |
| `motor.idle`     | Motor idle state               | `bool`    |

| Methods                                           | Description              | Arguments  |
| ------------------------------------------------- | ------------------------ | ---------- |
| `motor.speed(speed[, acceleration])`              | Move with given `speed`  | 2x `float` |
| `motor.position(position, speed[, acceleration])` | Move to given `position` | 3x `float` |
| `motor.stop()`                                    | Stop                     |            |

The optional acceleration argument defaults to 0, which starts and stops pulsing immediately.

## Motor Axis

The motor axis module wraps a motor and two limit switches.
It prevents the motor from moving past the limits.
But in contrast to a simple Lizard rule, it allows to actively move out of the limits when moving in the right direction.
Currently supported motor types are CanOpenMotor, ODriveMotor and StepperMotor.

| Constructor                               | Description             | Arguments |
| ----------------------------------------- | ----------------------- | --------- |
| `axis = MotorAxis(motor, limit1, limit2)` | motor and input modules | 3 modules |

Currently the motor axis module has no properties.
To get the current position or speed, access the motor module instead.

| Methods                                           | Description              | Arguments  |
| ------------------------------------------------- | ------------------------ | ---------- |
| `motor.speed(speed[, acceleration])`              | Move with given `speed`  | 2x `float` |
| `motor.position(position, speed[, acceleration])` | Move to given `position` | 3x `float` |
| `motor.stop()`                                    | Stop                     |            |

## CanOpenMaster

The CanOpenMaster module sends periodic SYNC messages to all CANopen nodes. At creation, no messages are sent until `sync_interval` is set to a value greater than 0.

| Constructor                      | Description | Arguments  |
| -------------------------------- | ----------- | ---------- |
| `co_master = CanOpenMaster(can)` | CAN module  | CAN module |

| Properties                | Description                                 | Data type |
| ------------------------- | ------------------------------------------- | --------- |
| `co_master.sync_interval` | Amount of lizard steps in between each SYNC | `int`     |

## CanOpenMotor

The CanOpenMotor module implements a subset of commands necessary to control a motor implementing DS402.
Positional and velocity units are currently undefined and must by manually measured.
Once the configuration sequence has finished, current status, position and velocity are queried on every SYNC.

| Constructor                          | Description                     | Arguments         |
| ------------------------------------ | ------------------------------- | ----------------- |
| `motor = CanOpenMotor(can, node_id)` | CAN module and node ID (1..127) | CAN module, `int` |

| Methods                                                   | Description                                                                               | Arguments |
| --------------------------------------------------------- | ----------------------------------------------------------------------------------------- | --------- |
| `motor.enter_pp_mode(velo)`                               | Set 402 operating mode to profile position, halt off, and target velocity to `velo`       | `int`     |
| `motor.enter_pv_mode()`                                   | Set 402 operating mode to profile velocity, halt on, and target velocity to `velo`        | `int`     |
| `motor.set_target_position(pos)`                          | Set target position to `pos` (signed). [pp mode]                                          | `int`     |
| `motor.commit_target_position()`                          | Instruct motor to move to previously set target position. [pp mode]                       |           |
| `motor.set_target_velocity(velo)`                         | Set target velocity to `velo`. Absolute for pp mode, signed for pv mode                   | `int`     |
| `motor.set_ctrl_halt(mode)`                               | Latches / resets the "halt" bit and sends the updated control word to the node            | `bool`    |
| `motor.set_ctrl_enable(mode)`                             | Latches / resets the "enable operation" bit and sends an updated control word to the node | `bool`    |
| `motor.set_profile_acceleration(acceleration)`            | Sets the motor acceleration                                                               | `int`     |
| `motor.set_profile_deceleration(deceleration)`            | Sets the motor deceleration                                                               | `int`     |
| `motor.set_profile_quick_stop_deceleration(deceleration)` | Sets the motor deceleration for the quick stop command                                    | `int`     |
| `motor.reset_fault()`                                     | Clear any faults (like positioning errors). Implicitly sets the "halt" bit.               |           |
| `motor.sdo_read(index)`                                   | Performs an SDO read at index `index` and sub index `0x00`                                | `int`     |

| Properties              | Description                                              | Data type |
| ----------------------- | -------------------------------------------------------- | --------- |
| `initialized`           | Concurrent init sequence has finished, motor is ready    | `bool`    |
| `last_heartbeat`        | Time in µs since bootup when last heartbeat was received | `int`     |
| `is_booting`            | Node is in booting state                                 | `bool`    |
| `is_preoperational`     | Node is in pre-operational state                         | `bool`    |
| `is_operational`        | Node is in operational state                             | `bool`    |
| `actual_position`       | Motor position at last SYNC                              | `int`     |
| `position_offset`       | Offset implicitly added to target/reported position      | `int`     |
| `actual_velocity`       | Motor velocity at last SYNC                              | `int`     |
| `status_enabled`        | Operation enabled bit of status word since last SYNC     | `bool`    |
| `status_fault`          | Fault bit of status word since last SYNC                 | `bool`    |
| `status_target_reached` | Target reached bit of status word since last SYNC        | `bool`    |
| `ctrl_enable`           | Latched operation enable bit of every sent control word  | `bool`    |
| `ctrl_halt`             | Latched halt bit of every sent control word              | `bool`    |

**Configuration sequence**

After creation of the module, the configuration is stepped through automatically on each heartbeat; once finished, the `initialized` attribute is set to `true`.
Note that for runtime variables (actual position, velocity, and status bits) to be updated, a CanOpenMaster module must exist and be sending periodic SYNCs.

**Target position sequence**

Note: The target velocity must be positive regardless of target point direction.
The halt bit is cleared when entering pp, though it can be set at any point during moves to effectively apply brakes.

```
// First time, assuming motor is disabled and not in pp mode
motor.set_ctrl_enable(true)
motor.enter_pp_mode(<some positive velocity>)

// All further set points only need these
motor.set_target_position(<some position>)
motor.commit_target_position()
```

**Target velocity sequence**

Unlike in the profile position mode, here the sign of the velocity does controls the direction.
The halt bit is set when entering pv. To start moving, clear it (and set again to stop).

```
// First time, assuming motor is disabled and not in pv mode
motor.set_ctrl_enable(true)
motor.enter_pv_mode(<some signed velocity>)

// Further movements only need these
motor.set_ctrl_halt(false)
// await some condition
motor.set_ctrl_halt(true)
```

## D1 Motor

This module controls an [igus D1 motor controller](https://www.igus.eu/product/D1) via CANOpen.

| Constructor                     | Description                     | Arguments         |
| ------------------------------- | ------------------------------- | ----------------- |
| `motor = D1Motor(can, node_id)` | CAN module and node ID (1..127) | CAN module, `int` |

| Properties                   | Description                             | Data type |
| ---------------------------- | --------------------------------------- | --------- |
| `motor.switch_search_speed`  | Speed for moving into the end stop      | `int`     |
| `motor.zero_search_speed`    | Speed for moving out ot the end stop    | `int`     |
| `motor.homing_acceleration`  | Acceleration for homing                 | `int`     |
| `motor.profile_acceleration` | Acceleration for profile movements      | `int`     |
| `motor.profile_velocity`     | Velocity for profile position movements | `int`     |
| `motor.profile_deceleration` | Deceleration for profile movements      | `int`     |
| `motor.position`             | Current position                        | `int`     |
| `motor.velocity`             | Current velocity                        | `int`     |
| `motor.status_word`          | Status word                             | `int`     |
| `motor.status_flags`         | Status flags                            | `int`     |

Bit 0 of the status flags indicates the motor is referenced (1) or not (0).

The status word (also called "Statusword") is a 16 bit integer with several flags. See the [D1 manual](https://www.igus.eu/product/D1) for details.

| Methods                                         | Description              | Arguments |
| ----------------------------------------------- | ------------------------ | --------- |
| `motor.setup()`                                 | Setup (enable) the motor |           |
| `motor.home()`                                  | Start homing the motor   |           |
| `motor.profile_position()`                      | Move to given position   | `int`     |
| `motor.profile_velocity()`                      | Move with given velocity | `int`     |
| `motor.stop()`                                  | Stop (disable) the motor |           |
| `motor.reset()`                                 | Reset motor errors       |           |
| `motor.sdo_read(index[, subindex])`             | Read SDO                 | 2x `int`  |
| `motor.sdo_write(index, subindex, bits, value)` | Write SDO                | 4x `int`  |
| `motor.nmt_write()`                             | Write NMT                | `int`     |

## DunkerMotor

This module controls [dunkermotoren](https://www.dunkermotoren.de/) motor via CANOpen.

| Constructor                         | Description                     | Arguments         |
| ----------------------------------- | ------------------------------- | ----------------- |
| `motor = DunkerMotor(can, node_id)` | CAN module and node ID (1..127) | CAN module, `int` |

| Properties         | Description                     | Data type |
| ------------------ | ------------------------------- | --------- |
| `motor.speed`      | Motor speed (meters per second) | `float`   |
| `motor.m_per_turn` | Meters per turn                 | `float`   |
| `motor.reversed`   | Reverse motor direction         | `bool`    |

| Methods                                         | Description                   | Arguments |
| ----------------------------------------------- | ----------------------------- | --------- |
| `motor.speed(speed)`                            | Move with given `speed` (m/s) | `float`   |
| `motor.enable()`                                | Enable motor                  |           |
| `motor.disable()`                               | Disable motor                 |           |
| `motor.sdo_read(index[, subindex])`             | Read SDO                      | 2x `int`  |
| `motor.sdo_write(index, subindex, bits, value)` | Write SDO                     | 4x `int`  |

## DunkerWheels

The DunkerWheels module combines two DunkerMotor modules and provides odometry and steering for differential wheeled robots.

| Constructor                                      | Description           | Arguments               |
| ------------------------------------------------ | --------------------- | ----------------------- |
| `wheels = DunkerWheels(left_motor, right_motor)` | left and right motors | two DunkerMotor modules |

| Properties             | Description           | Data type |
| ---------------------- | --------------------- | --------- |
| `wheels.width`         | Wheel distance (m)    | `float`   |
| `wheels.linear_speed`  | Forward speed (m/s)   | `float`   |
| `wheels.angular_speed` | Turning speed (rad/s) | `float`   |

| Methods                         | Description                                     | Arguments        |
| ------------------------------- | ----------------------------------------------- | ---------------- |
| `wheels.speed(linear, angular)` | Move with `linear`/`angular` speed (m/s, rad/s) | `float`, `float` |

## Analog Input

This module is designed for reading analog voltages and converting them to digital values using the ESP32's ADC units.
For detailed specifications of the ESP32 ADC modules, including attenuation levels, voltage range mappings, and GPIO-to-channel mapping, check the ESP32 documentation.

| Constructor                                     | Description                              | Arguments             |
| ----------------------------------------------- | ---------------------------------------- | --------------------- |
| `analog = Analog(unit, channel[, attenuation])` | unit, channel and attenuation level (dB) | `int`, `int`, `float` |

Possible attenuation levels are 0, 2.5, 6, and 12 dB.
The default attenuation level is 12 dB.

| Properties | Description                    | Data type |
| ---------- | ------------------------------ | --------- |
| `raw`      | raw measurement value (0-4095) | `int`     |
| `voltage`  | voltage (V)                    | `float`   |

## Expander

The expander module allows communication with another microcontroller connected via [serial](#serial-interface).

| Constructor                                   | Description                        | Arguments               |
| --------------------------------------------- | ---------------------------------- | ----------------------- |
| `expander = Expander(serial[, boot, enable])` | Serial module and boot/enable pins | Serial module, 2x `int` |

| Methods                 | Description                                      | Arguments |
| ----------------------- | ------------------------------------------------ | --------- |
| `expander.run(command)` | Run any `command` on the other microcontroller   | `string`  |
| `expander.disconnect()` | Disconnect serial connection and pins            |           |
| `expander.flash()`      | Flash other microcontroller with own binary data |           |

The `flash()` method requires the `boot` and `enable` pins to be defined.

The `disconnect()` method might be useful to access the other microcontroller on UART0 via USB while still being physically connected to the main microcontroller.

Note that the expander forwards all other method calls to the remote core module, e.g. `expander.info()`.

| Properties         | Description                                             | Data type |
| ------------------ | ------------------------------------------------------- | --------- |
| `last_message_age` | Time since last message from other microcontroller (ms) | `int`     |

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
