# Dual Drive CAN Protocol

CAN ID formula: `CanID = (NodeID << 5) | CmdID`

- CAN 2.0A, 8-byte frames, little-endian
- Baudrate: 250 kbit/s (current firmware, doc says 1 Mbit/s)

## Commands (Lizard -> Motor)

### 0x01 Speed

Velocity command for track drives.

| Byte | Type  | Description                                   |
| ---- | ----- | --------------------------------------------- |
| 0-1  | int16 | AngularVel, scaled by 0.01 rad/s per LSB      |
| 2    | uint8 | AccLim (0x00 = default, 0xFF = unlimited)     |
| 3    | int8  | JerkLimExp (0x00 = default, 0xFF = unlimited) |
| 4-7  |       | Reserved                                      |

Note: The protocol spec says factor 0.001 rad/s, but actual firmware uses 0.01 rad/s (confirmed with Innotronic).

### 0x02 RelAngle

Relative angle command for track drives.

| Byte | Type   | Description                                       |
| ---- | ------ | ------------------------------------------------- |
| 0-1  | int16  | RelAngle in 0.001 rad per LSB                     |
| 2-3  | uint16 | VelLim in 0.01 rad/s per LSB (0xFFFF = unlimited) |
| 4    | uint8  | AccLim (0x00 = default, 0xFF = unlimited)         |
| 5    | int8   | JerkLimExp (0x00 = default, 0xFF = unlimited)     |
| 6-7  |        | Reserved                                          |

### 0x03 AngleCmd (Delta Arm)

Per-motor position command for delta arm motors.

| Byte | Type   | Description                                                 |
| ---- | ------ | ----------------------------------------------------------- |
| 0    | uint8  | Motor select: 0x10 = left (motor 1), 0x20 = right (motor 2) |
| 1-2  | int16  | Position in hall ticks (+-150, where 150 = 180 degrees)     |
| 3-4  | uint16 | Speed limit (lower = slower, 0xFFFF = unlimited)            |
| 5-7  |        | Reserved                                                    |

### 0x0A SwitchState

| Byte | Type  | Description                               |
| ---- | ----- | ----------------------------------------- |
| 0    | uint8 | 1 = Off (de-energised), 2 = Brake, 3 = On |
| 1-7  |       | Reserved                                  |

### 0x0B Configure

| Byte | Type   | Description |
| ---- | ------ | ----------- |
| 0    | uint8  | SettingID   |
| 1    |        | Reserved    |
| 2-3  | uint16 | Value1      |
| 4-7  | int32  | Value2      |

Setting IDs:

- 0x01: Set CAN node ID. Value1 = new base address (`new_node_id << 5`), Value2 = 0
- 0x02: Switch between drive and delta arm mode. Value1 = 0xA5A5 (drive) or 0xB5B5 (delta arm), Value2 = 0

### 0x0C ReferenceDriveCmd / SingleMotorControl

Controls individual motors for reference drive and braking.

| Byte | Type  | Description                                                                                    |
| ---- | ----- | ---------------------------------------------------------------------------------------------- |
| 0    | uint8 | Command motor 1: 0x00 = no action, 0x05 = brake, 0x10 = calibration CW, 0x20 = calibration CCW |
| 1    | uint8 | Command motor 2: same values as above                                                          |
| 2-7  |       | Reserved                                                                                       |

Both bytes can be set simultaneously to drive both motors at once.

## Status Messages (Motor -> Lizard)

### 0x11 MotorStatus (cyclic)

Sent automatically by the motor controller.

| Byte | Type   | Description                        |
| ---- | ------ | ---------------------------------- |
| 0-1  | int16  | AngularVel, 0.01 rad/s per LSB     |
| 2-3  | int16  | Voltage, 0.01 V per LSB            |
| 4    | int8   | Temperature in degrees C           |
| 5    | uint8  | State (matches SwitchState values) |
| 6-7  | uint16 | ErrorCodes bitmask                 |

Note: Bytes 2-3 were originally current in the spec but are voltage in actual firmware.

ErrorCode bits (from firmware):

- 0x0004: VOR (drive direction)
- 0x0008: RUECK (motor running direction)
- 0x0010: SERVICEMODE (RS232 active)
- 0x0020: HALL_ERROR (hall sensor defective)
- 0x0040: CONNECTION_ERROR (motor B not connected)
- 0x0080: HW_I_FAULT (hardware overcurrent)

### 0x12 DriveStatus (cyclic in drive mode, ~100ms)

Per-motor speed and current. Replaces the old 0x13 layout (fields reordered: speed first, current after).

| Byte | Type  | Description                            |
| ---- | ----- | -------------------------------------- |
| 0-1  | int16 | AngularVel motor 1, 0.01 rad/s per LSB |
| 2-3  | int16 | AngularVel motor 2, 0.01 rad/s per LSB |
| 4-5  | int16 | Current motor 1, 0.001 A per LSB       |
| 6-7  | int16 | Current motor 2, 0.001 A per LSB       |

### 0x13

~was removed~

### 0x14 ReferenceDriveResult (event-based)

Sent by motor controller when reference drive completes (success or error).

| Byte | Type  | Description                                                |
| ---- | ----- | ---------------------------------------------------------- |
| 0    | uint8 | Upper nibble: motor 1 result, lower nibble: motor 2 result |
| 1-7  |       | Reserved                                                   |

Result values per motor:

- 0: No result
- 1: OK, reference position stored
- 2: Overcurrent, current limit reached
- 4: Ref end, max rotation (300 digits) reached without endstop

### 0x15 CurrentAngleCurrent (cyclic in delta mode, ~100ms)

Sent automatically when the motor controller is in delta arm mode.
Combines per-motor angle and current into a single frame (position controller mode).

| Byte | Type  | Description                      |
| ---- | ----- | -------------------------------- |
| 0-1  | int16 | Angle motor 1 (hall ticks)       |
| 2-3  | int16 | Angle motor 2 (hall ticks)       |
| 4-5  | int16 | Current motor 1, 0.001 A per LSB |
| 6-7  | int16 | Current motor 2, 0.001 A per LSB |
