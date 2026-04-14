# Dual Drive TODO

## Open

- [ ] Make reference drive current limit configurable (currently hardcoded in firmware, needs Configure setting from Innotronic)
  -> neue version mit hohen wert
- [x] `switch_to_delta_mode()` / `switch_to_drive_mode()`: Configure 0x0B, setting_id 0x02, Value1 = 0xB5B5 (delta) / 0xA5A5 (drive)
- [ ] Reference drive: implement ref_stop zero-angle reset via Configure command (setting_id TBD from Innotronic)
- [ ] Clarify 0x11 bytes 2-3 voltage scaling factor with Innotronic
  board voltage oderso
- [x] Clarify if 0x12 angle report works for drive motors — confirmed: 0x12 is DriveStatus (speed+current) only in drive mode; delta mode uses 0x15
- [x] 0x15 CurrentAngleCurrent: current is transmitted in milliamps (confirmed by Innotronic) — fixed scaling from 0.095 A/LSB to mA
- [ ] AngleCmd (0x03) speed_limit: will be unblocked by Innotronic, range 1-50 (1 = fast, 50 = slow)
- [ ] SwitchState: not properly implemented for delta arm on firmware side — Innotronic will fix
- [ ] off -> on drives up sometimes (should not) — Innotronic will fix (switch_state 1)
- [x] Reference clockwise bug — fixed on our side, Innotronic will also fix on firmware side
- [x] Delta arm motor has 300 ticks/rev (not 200) — updated DELTA_MOTOR_TICKS
