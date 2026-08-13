#!/usr/bin/env python3
"""
State-machine simulation for Expander is_ready fix (#266).

Simulates the core Expander logic to verify that is_ready correctly
transitions after restart(), flash(), and ping timeout scenarios.

This is NOT a unit test of the C++ code — it's a behavioral simulation
of the state machine to verify the fix logic before hardware testing.
"""

import time
from dataclasses import dataclass, field


@dataclass
class MockSerial:
    """Simulates the serial link between core and P0."""
    buffer: list[str] = field(default_factory=list)

    def has_buffered_lines(self) -> bool:
        return len(self.buffer) > 0

    def read_line(self) -> str:
        return self.buffer.pop(0) if self.buffer else ""

    def write(self, line: str):
        # Simulate P0 receiving commands; for this simulation we just track them
        pass

    def inject_p0_response(self, line: str):
        """Simulate P0 sending a line to core."""
        self.buffer.append(line)


class ExpanderSimulation:
    """Simulates the Expander module state machine with the fix."""

    def __init__(self, boot_timeout: float = 5.0, ping_interval: float = 1.0, ping_timeout: float = 2.0):
        self.serial = MockSerial()
        self.boot_timeout_ms = boot_timeout * 1000
        self.ping_interval = ping_interval
        self.ping_timeout = ping_timeout

        self.is_ready = False
        self.boot_start_time = 0
        self.last_message_millis = 0
        self.ping_pending = False

        self.log: list[str] = []

    def millis(self) -> int:
        return int(time.time() * 1000)

    def millis_since(self, then: int) -> int:
        return self.millis() - then

    def check_boot_progress(self):
        while self.serial.has_buffered_lines():
            line = self.serial.read_line()
            self.last_message_millis = self.millis()
            self.log.append(f"  check_boot_progress: received '{line}'")
            if line == "Ready.":
                self.is_ready = True
                self.log.append("  ✓ is_ready = true (boot complete)")
                break

    def ping(self):
        last_message_age = (self.millis() - self.last_message_millis) / 1000.0
        if not self.ping_pending:
            if last_message_age >= self.ping_interval:
                self.log.append(f"  ping: sending PONG request (age={last_message_age:.1f}s)")
                self.ping_pending = True
        else:
            if last_message_age >= self.ping_interval + self.ping_timeout:
                self.log.append(f"  ✗ ping timeout (age={last_message_age:.1f}s)")
                self.is_ready = False
                self.boot_start_time = 0  # THE FIX: close boot window on ping timeout

    def handle_messages(self):
        while self.serial.has_buffered_lines():
            line = self.serial.read_line()
            self.last_message_millis = self.millis()
            self.ping_pending = False
            self.log.append(f"  handle_messages: received '{line}'")

    def step(self):
        if self.is_ready:
            self.ping()
            self.handle_messages()
        else:
            # THE FIX: re-enter boot detection when !is_ready
            if self.boot_timeout_ms == 0 or self.millis_since(self.boot_start_time) <= self.boot_timeout_ms:
                self.check_boot_progress()

    def restart(self):
        self.log.append("restart() called")
        self.ping_pending = False
        self.boot_start_time = self.millis()
        self.is_ready = False
        self.log.append(f"  is_ready = false, boot_start_time reset")

    def simulate_p0_boot(self, delay_ms: int = 100):
        """Simulate P0 booting and sending Ready. after a delay."""
        self.log.append(f"  P0 booting (will send Ready. in {delay_ms}ms)...")
        time.sleep(delay_ms / 1000.0)
        self.serial.inject_p0_response("Ready.")


def test_restart_recovery():
    """Test that is_ready recovers after restart()."""
    print("\n=== Test: restart() recovery ===")
    exp = ExpanderSimulation()

    # Initial boot
    exp.boot_start_time = exp.millis()
    exp.serial.inject_p0_response("Ready.")
    exp.step()
    assert exp.is_ready, "Should be ready after initial boot"
    print("✓ Initial boot: is_ready = true")

    # Simulate restart
    exp.restart()
    assert not exp.is_ready, "Should not be ready after restart"
    print("✓ After restart(): is_ready = false")

    # Simulate P0 reboot
    exp.simulate_p0_boot(delay_ms=50)
    exp.step()
    assert exp.is_ready, "Should be ready after P0 reboots"
    print("✓ After P0 reboot: is_ready = true")
    print("✅ PASS: restart() recovery works")


def test_flash_recovery():
    """Test that is_ready recovers after flash() (which calls restart())."""
    print("\n=== Test: flash() recovery ===")
    exp = ExpanderSimulation()

    # Initial boot
    exp.boot_start_time = exp.millis()
    exp.serial.inject_p0_response("Ready.")
    exp.step()
    assert exp.is_ready
    print("✓ Initial boot: is_ready = true")

    # Simulate flash (which calls restart at the end)
    exp.log.append("flash() called (simulated)")
    exp.restart()  # flash() calls restart() at the end
    assert not exp.is_ready
    print("✓ After flash(): is_ready = false")

    # Simulate P0 reboot after flash
    exp.simulate_p0_boot(delay_ms=100)
    exp.step()
    assert exp.is_ready, "Should be ready after P0 reboots post-flash"
    print("✓ After P0 reboot: is_ready = true")
    print("✅ PASS: flash() recovery works")


def test_ping_timeout_no_boot_detection():
    """Test that ping timeout does NOT re-enter boot detection."""
    print("\n=== Test: ping timeout does not re-enter boot detection ===")
    exp = ExpanderSimulation(boot_timeout=5.0, ping_interval=0.5, ping_timeout=0.5)

    # Initial boot
    exp.boot_start_time = exp.millis()
    exp.serial.inject_p0_response("Ready.")
    exp.step()
    assert exp.is_ready
    print("✓ Initial boot: is_ready = true")

    # Wait for boot window to close
    time.sleep(0.1)  # Small delay to ensure we're past boot window
    exp.boot_start_time = exp.millis() - 10000  # Simulate old boot_start_time

    # Simulate ping timeout
    exp.last_message_millis = exp.millis() - 2000  # 2s ago
    exp.ping_pending = True
    exp.step()
    assert not exp.is_ready, "Should not be ready after ping timeout"
    assert exp.boot_start_time == 0, "boot_start_time should be 0 after ping timeout"
    print("✓ After ping timeout: is_ready = false, boot_start_time = 0")

    # Inject a message — it should NOT be consumed by check_boot_progress
    # because boot_start_time is 0 (boot window closed)
    exp.serial.inject_p0_response("some message")
    exp.step()
    assert not exp.is_ready, "Should still not be ready"
    # The message should still be in the buffer (not consumed)
    assert exp.serial.has_buffered_lines(), "Message should not be consumed when boot window is closed"
    print("✓ Messages not consumed after ping timeout (boot window closed)")
    print("✅ PASS: ping timeout does not re-enter boot detection")


def test_boot_timeout_expires():
    """Test that boot detection stops after boot_timeout."""
    print("\n=== Test: boot timeout expires ===")
    exp = ExpanderSimulation(boot_timeout=0.5)  # 500ms timeout

    # Simulate restart
    exp.restart()
    assert not exp.is_ready
    print("✓ After restart(): is_ready = false")

    # Wait for boot timeout to expire
    time.sleep(0.6)
    exp.step()
    assert not exp.is_ready, "Should not be ready after boot timeout"
    print("✓ After boot timeout: is_ready still false (no Ready. received)")

    # Now send Ready. — it should NOT be picked up because boot window expired
    exp.serial.inject_p0_response("Ready.")
    exp.step()
    assert not exp.is_ready, "Should not pick up Ready. after boot timeout expired"
    assert exp.serial.has_buffered_lines(), "Ready. should still be in buffer"
    print("✓ Ready. not picked up after boot timeout expired")
    print("✅ PASS: boot timeout works correctly")


def test_fast_boot_ping_timeout_edge_case():
    """Test the edge case: fast boot + ping timeout within boot window."""
    print("\n=== Test: fast boot + ping timeout within boot window ===")
    exp = ExpanderSimulation(boot_timeout=5.0, ping_interval=0.3, ping_timeout=0.3)

    # Initial boot (fast)
    exp.boot_start_time = exp.millis()
    exp.serial.inject_p0_response("Ready.")
    exp.step()
    assert exp.is_ready
    print("✓ Initial boot: is_ready = true")

    # Immediately cause a ping timeout (within boot window)
    exp.last_message_millis = exp.millis() - 1000  # 1s ago
    exp.ping_pending = True
    exp.step()
    assert not exp.is_ready, "Should not be ready after ping timeout"
    assert exp.boot_start_time == 0, "boot_start_time should be 0"
    print("✓ Ping timeout within boot window: boot_start_time = 0")

    # Verify boot detection does not activate
    exp.serial.inject_p0_response("some message")
    exp.step()
    assert exp.serial.has_buffered_lines(), "Message should not be consumed"
    print("✓ Boot detection does not activate after ping timeout")
    print("✅ PASS: edge case handled correctly")


if __name__ == "__main__":
    print("Expander is_ready state-machine simulation (#266)")
    print("=" * 60)

    try:
        test_restart_recovery()
        test_flash_recovery()
        test_ping_timeout_no_boot_detection()
        test_boot_timeout_expires()
        test_fast_boot_ping_timeout_edge_case()

        print("\n" + "=" * 60)
        print("✅ ALL TESTS PASSED")
        print("=" * 60)
    except AssertionError as e:
        print(f"\n❌ TEST FAILED: {e}")
        raise
