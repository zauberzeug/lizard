# Dual Drive TODO

## Open (Current)

- [ ] SwitchState: shipped in 2.41 firmware but untested — verify delta arm behaviour
  - [ ] off -> on drives up sometimes (should not) — switch_state 1
- [ ] Understandable speed values in 0x03 AngleCmd (currently 1-50, no physical unit)
- [ ] Can Bus reports off (randomly?) < f27 problem?

### drive

- [x] Robot is driving "slow" < validate with operator
- [ ] unstable on low speeds < test on f24

## Later

- [ ] Handle multiple CAN commands coming directly (e.g. trajectories or separate position commands for delta arm)
- [ ] 0x0B Configure: cyclic repeat interval for motor status messages still missing
- [ ] Increase CAN baudrate to 500 kbit/s (1 Mbit/s if possible)

## Lizard Problems

- [ ] estop results in a need to restart the esp32, since CAN is not waking up again
  - [ ] verify fix: `reset_can_bus()` now handles BUS_OFF via `twai_initiate_recovery()` directly (previously tried `twai_stop()` from BUS_OFF, which fails and skipped recovery). Also clears TX/RX queues before restart so stale frames don't re-trigger bus-off. Test on hardware with the scenarios from `can fehler.md`.
- [ ] Can Bus reports off (randomly?) — may also be addressed by the `reset_can_bus()` fix above; verify on hardware
- [ ] 0x11 MotorStatus: voltage scaling factor fixed (0.01 → 0.001) — verify on hardware

## Questions

- [ ] 0x11 MotorStatus `angular_vel`: which motor does this represent? In drive mode 0x12 already provides both per-motor speeds (redundant). In delta mode we'd need speeds for both motors — a single value is not enough, and 0x15 is already full. How should delta-mode per-motor speeds be reported?
- [ ] Drive mode has no position feedback (0x12 carries speed+current, not angle). Is that intentional, or should position be added somewhere for odometry / motor_axis use?
- [ ] 0x0C 0x05 semantics: does it always store the current position as 0-reference, or only while a reference drive is active? Lizard currently treats it as pure "stop/brake" when sent outside ref drive (e.g. endstop safety, single-motor stop).
