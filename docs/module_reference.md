# Module Reference

## General 

Common settings and commands for all modules.

### Settings

    set <name>.<key>=<value>
    output	serial output (“0” or “1”)

### Commands

```<name> get```
: Get current output of the module.

```<name> mute```
: Turn serial output for this module off.

```<name> unmute```
: Turn serial output on.

## ESP (static)

Core functionality of the main ESP32 microcontroller.

### Settings

    set esp.<key>=<value>

Where ```<key>``` can be: 

```ready```
: toggle the “RDYP” pin (“0” or “1”, default “0”)

```24v```
: toggle 24V output (“0” or “1”, default “0”)

```outputModules```
: comma-separated list of modules for one-line output

### Commands

```esp restart```
: Restart the microcontroller.

```esp print <message>```
: Print a message to the serial terminal.

```esp erase```
: Erase the persistent storage.

```esp ping```
: Do nothing. Can be used as a keep-alive signal (see Safety module).

```esp stop```
: Stop all actuators.

## Safety (static)

Conditions: A condition can affect one or all modules. 
If a condition is fulfilled, a command is executed on the dependent module. 
The condition is checked in every main loop cycle.

Shadows: Messages to a trigger module are sent to its shadow module as well.

### Settings

    set safety.<key>=<value>

```active```
: enable/disable this module (“0” or “1”)

```keep_alive_interval```
: maximum duration without message (s, disabled when 0)

### Conditions

```if <trigger> == <state> <msg>```
: If the <trigger> module is in the specified <state>, the <msg> is evaluated.

```if <trigger> != <state> <msg>```
: A comparison can also check for inequality using.

```if <trigger1> == <state1> && <trigger2> == <state2> <msg>```
: Multiple conditions that have to hold simultaneously can be combined.

### Shadows

```shadow <source> > <target>```
```shadow <source> <> <target>```
: Every command to the <source> module is mirrored to the <target> module. Using the “>” symbol indicates one-way shadowing and “<>” shadows in both directions.

### Commands

```safety list```
: Print condition with their current values (“ok” or “violated”) as well as all shadows.

## Bluetooth

Communication with other bluetooth devices.

### Constructor

    new bluetooth <name> <device>

```device```
: advertised device name

### Commands

    <name> send <device>,<service>,<characteristic>,<message>

Send a Message to a bluetooth device identified via device name, service ID and characteristic ID.

## CAN

A generic can bus.

### Constructor

    new can <name> <rx>,<tx>

```rx```
: RX port name

```tx```
: TX port name

### Commands

```<name> send <msgId>[,<d0>[,...[,<d7>]]]```
: Send a packet with a message ID and up to 8 data bytes (numbers in base 16).

```<name> request <msgId>[,<d0>[,...[,<d7>]]]```
: Send an RTR packet with a message ID and up to 8 data bytes (numbers in base 16).

### Output
```<name> receive <msgId>,<d0>,...,<d7>```
: A received packet with message ID and 8 data bytes (numbers in base 16).

## IMU

Internal IMU.

### Constructor

    new imu <name>

### Output

```<name> <euler_x> <euler_y> <euler_z>```
: Current rotation (three euler angles in degrees).

## LED

Light signal control.

### Constructor

    new led <name> <port>

```port```
: port name

### Settings

    set <name>.<key>=<value>

```interval```
: pulse interval (in seconds)

```duty```
: pulse duty cycle (0..1)

```repeat```
: whether to turn off after first pulse (0) or repeat pulses (1, default)

### Commands

```<name> on```
: Switch LED on (with set brightness).

```<name> off```
: Switch LED off.

```<name> pulse```
: Pulse LED  (with set brightness and interval).

### Output

```<name> <level>```
: Current level (“0” or “1”).

## Button

A simple push button.

### Constructor

    new button <name> <port>

```port```
: port name

### Settings

    set <name>.<key>=<value>

```pullup```
: internal pull-up resistor (“0” or “1”)
```invert```
: invert state (“0” or “1”)

### Output

```<name> <state>```
: Current state (“0” or “1”).

## DC Motor

A simple DC motor.

### Constructor

    new dcmotor <name> <dir_port>,<pwm_port>

```dir_port```
: direction port

```pwm_port```
: PWM port

### Commands

```<name> power <power>```
: Move the motor with given power (-1..1).
```<name> stop```
: Stop the motor.

### Output

    <name> <state>

Current state.

### States

```0 STOP```
: stopped

```1 MOVE_FORWARD```
: moving inwards

```2 MOVE_BACKWARD```
: moving outwards

## Linear motor

A linear motor, possibly with position feedback.

### Constructor

    new  linearmotor <name> <portA>,<portB>[,<limitA>[,<limitB>]]

```portA```
: port name for inward motion

```portB```
: port name for outward motion

```limitA```
: port name for inward limit switch

```limitB```
: port name for outward limit switch

### Commands

```<name> in```
: Pull the piston in.

```<name> out```
: Push the piston out.

```<name> stop```
: Stop the actuator.

### Output

	<name> <state>

Current state.

### States

```0 STOP```
: stopped, no endswitch active

```1 STOP_IN```
: stopped, inward endswitch active

```2 STOP_OUT```
: stopped, outward endswitch active

```3 MOVE_IN```
moving inwards

```4 MOVE_OUT```
moving outwards

## RMD Motor

CAN-based communication with a Qyems motor like the RMD-X8. 

### Constructor

    new rmdmotor <name> <can>

```can```
: name of CAN module

### Settings

    set <name>.<key>=<value>

```ratio```
: transmission ratio from motor to output shaft (default: 6.0)

```speed```
: rotation speed (degrees per second, default: 10.0)

```minAngle```
: minimum rotation angle (degrees, default: -inf)

```maxAngle```
: maximum rotation angle (degrees, default: inf)

```tolerance```
: angular tolerance for state changes (degrees, default: 0.1)

### Commands

```<name> move <target>```
: Move to a specified target position (degrees, clipped to minAngle and maxAngle).

```<name> home```
: Move to zero position.

```<name> zero```
: Write current single turn position as zero position to ROM.

```<name> stop```
: Stop the motor. Keep holding the position.

```<name> off```
: Turn the motor off. Don’t try to hold the position.

```<name> clearError```
: Clear RMD error flags.

### Output

    <name> <state> <error> <position>

Current state, error flags and position (degrees).

### States

```0 STOP```
: stopped

```1 MOVE```
: moving

```2 HOME```
: homed

```3 HOMING```
: homing

```4 OFF```
: off

## RoboClaw motors

A RoboClaw controller with support for two DC motors.

### Constructor

    new roboclawmotors <name> [<address>[,<baud>]]

```address```
: driver address (optional, default: 128)

```baud```
: driver baud rate (optional, default: 38400)

### Settings

    set <name>.<key>=<value>

```accel1```
: acceleration for motor 1

```accel2```
: acceleration for motor 2

```homePw1```
: power of motor 1 for homing procedure

```homePw2```
: power of motor 2 for homing procedure

```homePos1```
: target position of motor 1 after homing procedure

```homePos2```
: target position of motor 2 after homing procedure

```point_<x>```
: key point with arbitrary name <x>

### Commands

```<name> power <power_1>,<power_2>```
: Drive with given motor power (0..1) for motors 1 and 2. Power 0 corresponds to 0V and power 1 to maximum voltage, respectively.

```<name> home```
: Perform homing procedure. Requires a limit switch for each motor.

```<name> move <position_1>,<position_2>,<duration>```
```<name> move <x>,<duration>```
: Move the motors to a given position in a given duration (seconds). The position can be given in ticks per motor or as the name of a key point.

```<name> stop```
: Stop both motors.

### Output

```
<name>
	<position_1>,<position_2>,
	<speed_1>,<speed_2>,
	<queue_size_1>,<queue_size_2>,
	<current_1>,<current_2>,
	<temperature>,<voltage>,<status_bits>,<status_string>
```
: Various status values for both motors 1 and 2. These fields are exemplary for the RoboClaw motor controller. The output may vary for other controllers: The position is in ticks. The speed is in ticks/s. The currents are given in amperes for both motors, the temperature is in degrees Celsius, the battery voltage in volts and status is a binary number containing multiple status bits as described in the RoboClaw manual.

```<name> home completed```
: Confirmation after completing the homing procedure.

```<name> move completed```
: Confirmation after completing a move command.

## RoboClaw wheels

A RoboClaw controlling two wheels for driving the robot around. It can react to a bumper connected to a dedicated port.

### Constructor

    new roboclawwheels <name> [<address>[,<baud>]]

```address```
: driver address (optional, default: 128)

```baud```
: driver baud rate (optional, default: 38400)

### Settings

    set <name>.<key>=<value>

```mPerTick```
: transmission (m per tick)

```width```
: track width (m)

```bumper_port```
: bumper port (internal pull-up, active high)

```bumper_debounce```
: debounce period when releasing the bumper (ms)

### Commands

```<name> power <left>,<right>```
: Drive with given motor power (0..1) for left and right wheel. Power 0 corresponds to 0V and power 1 to maximum voltage, respectively.

```<name> speed <linear>,<angular>```
: Drive with given linear (m/s) and angular (rad/s) speed.

```<name> stop```

Brake to a complete stop. This is similar to sending drive 0,0, but automatically switches off the motor control loop after 1 second.

### Output

```
<name> <linear_speed>,<angular_speed>,<state>,
       <current1>,<current2>,<temperature>,<battery>,<status>
```
: The linear speed is in m/s and the angular speed in rad/s. The state is either “idle”, “powering”, ”speeding”, “braking” or ”reversing”. The currents are given in amperes for both motors, the temperature is in degrees Celsius, the battery voltage in volts and status is a binary number containing multiple status bits as described in the RoboClaw manual.

```<name> bump start```
```<name> bump stop```
: Signal bumper state changes.

## ODrive motor

CAN-based communication with a single motor connected to an ODrive.

### Constructor

    new odrivemotor <name> <homeSwitch>,<can>,<id>

```homeSwitch```
: name of Button module for detecting the home position

```can```
: name of CAN module

```id```
: CAN device ID of the corresponding ODrive axis

### Settings

    set <name>.<key>=<value>

```mPerTick```
: transmission (m per tick, default: 0.01)

```minPos```
: minimum position (default: -inf m)

```maxPos```
: maximum position (default: inf m)

```tolerance```
: position tolerance for state changes (default: 0.005 m)

```moveSpeed```
: speed for moving to given position (default: inf m)

```homeSpeed```
: speed for homing procedure (default: -0.1 m/s)

```homePos```
: target position home command (default: 0.0 m)

```currentLimit```
: current limit (amperes, default: 10 A, sent during move command)

```heartbeatTimeout```
: maximum interval without heartbeat message (default: 0.0 s) Note: A timeout leads to error code 255.

### Commands

```<name> move <target>[,<speed>]```
: Move to a target position (m), clipped to minPos and maxPos.
Use an optional speed parameter (m/s) or the moveSpeed setting as fallback.

```<name> dmove <distance>[,<speed>]```
: Incrementally move to by a distance (m), clipped to minPos and maxPos.
Use an optional speed parameter (m/s) or the moveSpeed setting as fallback.

```<name> speed <speed>```
: Move with given speed (m/s).

```<name> home```
: Start homing procedure: Move with homeSpeed until homeSwitch is active.

```<name> stop```
: Stop and shut off the motor.

```<name> reboot```
: Reboot the ODrive.

```<name> clearError```
: Clear ODrive error flags.

### Output
	<name> <state> <error> <position>

Current state, error flags and position (m).

### States

```0 STOP```
: stopped

```1 MOVE```
: moving
	
```2 HOME```
: homed

```3 HOMING```
: homing

## ODrive wheels

An ODrive controlling a pair of wheels with one ODrive motor module each.

### Constructor

    new odrivewheels <name> <leftMotor>,<rightMotor>

```leftMotor```
: name of left ODrive motor module

```rightMotor```
: name of right ODrive motor module

### Settings

    set <name>.<key>=<value>

```width```
: distance between both wheels (default: 1.0 m)

```leftPowerFactor```
: left factor for power commands (signed, default: 1.0)

```rightPowerFactor```
: right factor for power commands (signed, default: 1.0)

```leftSpeedFactor```
: left factor for speed commands (signed, default: 1.0)

```rightSpeedFactor```
: right factor for speed commands (signed, default: 1.0)

### Commands

```<name> power <left>,<right>```
: Drive with given power (-1..1) for the left and right wheels.

```<name> speed <linear>,<angular>```
: Drive with given linear (m/s) and angular (rad/s) speed.

```<name> stop```
: Stop both axes.

### Output

	<name> <linear_speed>,<angular_speed>

The linear speed is in m/s and the angular speed in rad/s.