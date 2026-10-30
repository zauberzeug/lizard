"""Lizard's console line checksum "@xx" (see "Checksums" in docs/machine_safety.md), shared by the host tools."""
from typing import Optional, Tuple


def checksum(text: str) -> int:
    """Lizard's line checksum: the XOR of all bytes of the line."""
    result = 0
    for byte in text.encode():
        result ^= byte
    return result


def format_line(text: str) -> str:
    """Append the checksum and the line end to a line that goes to the microcontroller."""
    return f'{text}@{checksum(text):02x}\n'


def split_checksum(line: str) -> Tuple[str, Optional[int]]:
    """Split a line into its text and its "@xx" checksum, which is None if the line has none."""
    if line[-3:-2] == '@':
        try:
            return line[:-3], int(line[-2:], 16)
        except ValueError:
            pass
    return line, None
