# Dual Drive TODO

## Open

- [ ] SwitchState: not properly implemented for delta arm on firmware side — Innotronic will fix
- [ ] off -> on drives up sometimes (should not) — Innotronic will fix (switch_state 1)

## Later

- [ ] Handle multiple can commands comming directly (for example trajectories or seperate position commands for delta arm)
- [ ] Understandable "speed" values instead of 1-50
- [ ] Robot is driving "slow"

## Lizard Problems

- [ ] estop results in a need to restart the esp32, since can is not waking up again
