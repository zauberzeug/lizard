"""The ``core`` lines of ``core.output(...)`` and the offset between the microcontroller's clock and the host's."""
from collections import deque
from dataclasses import dataclass
from typing import Deque, List, Optional


@dataclass
class CoreLine:
    """A line ``core <values>...`` of ``core.output(...)``, e.g. ``core.output("core.millis motor.position")``."""
    values: List[str]  # the words after "core", as printed

    @property
    def millis(self) -> Optional[int]:
        """The first value if it is an integer, as when the output starts with ``core.millis`` like in RoSys."""
        try:
            return int(self.values[0])
        except (IndexError, ValueError):
            return None


def parse_core_line(text: str) -> Optional[CoreLine]:
    """Parse a line given without checksum, or return None if it is no ``core`` line."""
    words = text.split()
    if not words or words[0] != 'core':
        return None
    return CoreLine(words[1:])


class ClockOffset:
    """The host time minus the microcontroller's time, averaged over the last samples.

    Feed it the host time a line with ``millis`` arrived at, e.g. of a ``core`` line or a telemetry frame.
    A sample that differs by more than ``jump`` seconds from the average, e.g. after a restart of the
    microcontroller, starts the average anew.
    """

    def __init__(self, history: int = 100, jump: float = 0.1) -> None:
        self.jump = jump
        self._samples: Deque[float] = deque(maxlen=history)
        self.value: Optional[float] = None
        """the offset in seconds, None until the first sample"""

    def update(self, host_time: float, millis: float) -> float:
        """Add a sample and return the new offset in seconds."""
        sample = host_time - millis / 1000
        if self.value is not None and abs(sample - self.value) > self.jump:
            self._samples.clear()
        self._samples.append(sample)
        self.value = sum(self._samples) / len(self._samples)
        return self.value

    def host_time(self, millis: float) -> Optional[float]:
        """Map a time of the microcontroller to host time, or return None while the offset is unknown."""
        return None if self.value is None else millis / 1000 + self.value

    def reset(self) -> None:
        self._samples.clear()
        self.value = None
