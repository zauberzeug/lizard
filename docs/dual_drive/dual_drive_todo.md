# Dual Drive TODO

## Open

- [ ] Make reference drive current limit configurable (currently hardcoded in firmware, needs Configure setting from Innotronic)
- [ ] `switch_to_delta_mode()`: Get correct Configure setting_id and values from Innotronic (currently placeholder)
- [ ] Reference drive: implement ref_stop zero-angle reset via Configure command (setting_id TBD from Innotronic)
- [ ] Clarify 0x11 bytes 2-3 voltage scaling factor with Innotronic
- [ ] Clarify if 0x12 angle report works for drive motors (not just delta)
- [ ] 0x15 CurrentAngleCurrent: confirm types and scaling with Innotronic (currently assumed int16 hall ticks + int16 at 0.095 A/LSB)
- [ ] Configure message interval via setting_id (CmdID + interval_ms, spec TBD)
- [ ] AngleCmd (0x03) speed_limit parameter has no effect on firmware side — check with Innotronic
