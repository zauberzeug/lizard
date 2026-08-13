#!/usr/bin/env python3
"""
Demonstrates the BUG: old step() logic without the fix.
Shows that is_ready never recovers after restart().
"""

import time
from dataclasses import dataclass, field


@dataclass
class MockSerial:
    buffer: list[str] = field(default_factory=list)
    def has_buffered_lines(self) -> bool:
        return len(self.buffer) > 0
    def read_line(self) -> str:
        return self.buffer.pop(0) if self.buffer else ""
    def inject_p0_response(self, line: str):
        self.buffer.append(line)


class OldExpanderSimulation:
    """Simulates the OLD (buggy) Expander — no boot detection in step()."""

    def __init__(self, boot_timeout=5.0):
        self.serial = MockSerial()
        self.boot_timeout_ms = boot_timeout * 1000
        self.is_ready = False
        self.boot_start_time = 0
        self.last_message_millis = 0
        self.ping_pending = False

    def millis(self) -> int:
        return int(time.time() * 1000)

    def millis_since(self, then: int) -> int:
        return self.millis() - then

    def check_boot_progress(self):
        while self.serial.has_buffered_lines():
            line = self.serial.read_line()
            self.last_message_millis = self.millis()
            if line == "Ready.":
                self.is_ready = True
                break

    def step(self):
        # OLD LOGIC: no boot detection when !is_ready
        if self.is_ready:
            # ping + handle_messages (simplified)
            pass

    def restart(self):
        self.ping_pending = False
        self.boot_start_time = self.millis()
        self.is_ready = False


print("=== OLD (buggy) behavior: restart() ===")
exp = OldExpanderSimulation()

# Initial boot
exp.boot_start_time = exp.millis()
exp.serial.inject_p0_response("Ready.")
exp.check_boot_progress()  # constructor calls this directly
assert exp.is_ready
print("✓ Initial boot: is_ready = true")

# Restart
exp.restart()
assert not exp.is_ready
print("✓ After restart(): is_ready = false")

# P0 reboots and sends Ready.
exp.serial.inject_p0_response("Ready.")
print("  P0 sends Ready. ...")

# step() runs — but old logic never calls check_boot_progress()
exp.step()
print(f"  After step(): is_ready = {exp.is_ready}")

# step() again
exp.step()
print(f"  After step() again: is_ready = {exp.is_ready}")

# The message is still in the buffer, unprocessed
print(f"  Ready. still in buffer: {exp.serial.has_buffered_lines()}")

if not exp.is_ready:
    print("\n❌ BUG CONFIRMED: is_ready stays false forever after restart()")
    print("   The P0's Ready. message is never read because step() doesn't")
    print("   call check_boot_progress() when is_ready is false.")
else:
    print("\n✅ Unexpected: is_ready recovered (should not happen with old logic)")
