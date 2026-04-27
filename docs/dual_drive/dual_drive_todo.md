# Dual Drive TODO

## Open (Current)

- [ ] Understandable speed values in 0x03 AngleCmd (currently 1-50, no physical unit)

- [ ] Implement new Delta Arm Motor

- [ ] Activate new Delta Arm Motor like switch to drive mode
  ==> CmdID `0x0B` Configure, new mode value `0xC5C5` (alongside existing `0xA5A5` drive, `0xB5B5` delta arm):

  ```
  | Byte | Type   | Description                          |
  | ---- | ------ | ------------------------------------ |
  | 0    | uint8  | 0x02 (SettingID = Switch Mode)       |
  | 1    |        | Reserved                             |
  | 2-3  | uint16 | 0xC5C5 (new delta arm motor)         |
  | 4-7  | int32  | 0                                    |
  ```

- [ ] Use drive motors as delta arm motors
  ==> CmdID `0x0B` Configure, new mode value `0xD5D5` (analogous to existing `0xB5B5` delta arm, but applied to the drive motors so they accept per-motor position / angle commands):

  ```
  | Byte | Type   | Description                          |
  | ---- | ------ | ------------------------------------ |
  | 0    | uint8  | 0x02 (SettingID = Switch Mode)       |
  | 1    |        | Reserved                             |
  | 2-3  | uint16 | 0xD5D5 (drive motors in delta mode)  |
  | 4-7  | int32  | 0                                    |
  ```

## Later

### Drive

- [ ] configurable drive motor parameters
  ==> CmdID `0x0B` Configure, new SettingID `0x03` (next free):

  ```
  | Byte | Type   | Description                          |
  | ---- | ------ | ------------------------------------ |
  | 0    | uint8  | 0x03 (SettingID = Drive Param)       |
  | 1    |        | Reserved                             |
  | 2-3  | uint16 | Parameter selector (TBD)             |
  | 4-7  | int32  | Parameter value                      |
  ```

- [ ] maybe different parameters for slow and fast drive mode

### Delta

- [ ] Handle multiple CAN commands coming directly (e.g. trajectories or separate position commands for delta arm)

### General

- [ ] 0x0B Configure: cyclic repeat interval for motor status messages (0x11, 0x12, 0x15) still missing
  ==> CmdID `0x0B` Configure, new SettingID `0x04` (next free):

  ```
  | Byte | Type   | Description                          |
  | ---- | ------ | ------------------------------------ |
  | 0    | uint8  | 0x04 (SettingID = Cyclic Interval)   |
  | 1    |        | Reserved                             |
  | 2-3  | uint16 | Target CmdID (0x11, 0x12, 0x15)      |
  | 4-7  | int32  | Interval in ms (0 = disable)         |
  ```
