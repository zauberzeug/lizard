"""Shared helpers for the host-side serial tools."""
import serial

BAUDRATES = (115200, 230400, 460800, 921600)


def checksum(text: str) -> int:
    result = 0
    for character in text:
        result ^= ord(character)
    return result


def _line_is_valid(line: str) -> bool:
    body, separator, received = line.rpartition('@')
    if not separator:
        return False
    try:
        return checksum(body) == int(received, 16)
    except ValueError:
        return False


def detect_baudrate(device_path: str, *, default: int = 115200) -> int:
    """Find the baud rate the ESP uses by probing: at the right rate Lizard's
    checksummed reply to `core.info()` decodes cleanly, at a wrong rate it does not."""
    for baudrate in BAUDRATES:
        try:
            with serial.Serial(device_path, baudrate, timeout=0.3) as port:
                port.reset_input_buffer()
                port.write(f'core.info()@{checksum("core.info()"):02x}\n'.encode())
                for _ in range(30):
                    if _line_is_valid(port.read_until(b'\r\n').decode('utf-8', 'ignore').strip()):
                        return baudrate
        except (serial.SerialException, OSError):
            pass
    return default
