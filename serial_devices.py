"""Serial device discovery shared by the Lizard host tools.

A Jetson Robot Brain reaches the microcontroller over a fixed platform UART that no USB
enumeration can see, so it has to be derived from the L4T version; a development host uses a
USB-UART bridge that pyserial enumerates, with a description that tells two of them apart.
"""
import glob
import os
import re
from pathlib import Path
from typing import List, NamedTuple, Optional, Set

TEGRA_RELEASE = Path('/etc/nv_tegra_release')
IS_JETSON = TEGRA_RELEASE.exists()

JETSON_UARTS = {35: '/dev/ttyTHS0', 36: '/dev/ttyTHS1'}  # by L4T (Linux for Tegra) major version

# What a dry run names when it cannot name a real device; deliberately not paths, so that
# nothing can open them and the printed command shows the situation the real run would hit.
NO_DEVICE = '<no serial device attached>'
AMBIGUOUS_DEVICE = '<several serial devices attached, none selected>'

# USB-serial nodes pyserial can miss, e.g. behind a macOS vendor driver. Only the call-out nodes
# ("cu."): opening the dial-in twin ("tty.") can block until a carrier appears, and listing both
# would turn one adapter into two candidates.
EXTRA_PATTERNS = [
    '/dev/cu.usbserial*',
    '/dev/cu.usbmodem*',
    '/dev/cu.wchusbserial*',
    '/dev/cu.SLAB_USBtoUART*',
]


class Device(NamedTuple):
    path: str
    description: str = ''

    def __str__(self) -> str:
        return f'{self.path} ({self.description})' if self.description else self.path


def jetson_uart() -> Optional[str]:
    """Return the Jetson's UART to the microcontroller, or None when not running on a Jetson.

    An unreadable L4T version raises instead of falling back to USB enumeration, which on a
    Jetson finds no microcontroller and would hide the real cause behind a missing-device error.
    """
    if not IS_JETSON:
        return None
    match = re.search(r'R(\d+)', TEGRA_RELEASE.read_text(encoding='utf-8'))
    if not match:
        raise RuntimeError(f'Cannot determine the L4T (Linux for Tegra) version from {TEGRA_RELEASE}')
    major = int(match.group(1))
    if major not in JETSON_UARTS:
        raise RuntimeError(f'Unsupported L4T (Linux for Tegra) version: {major}')
    return JETSON_UARTS[major]


def find_devices() -> List[Device]:
    """Return the serial devices that could be a microcontroller, in no meaningful order.

    Only USB devices qualify on a development host, where a Lizard always sits behind a bridge:
    listing built-in and virtual ports (`/dev/ttyS*`, Bluetooth) would turn an unambiguous match
    into a pointless question.
    """
    uart = jetson_uart()
    if uart is not None:
        return [Device(uart, 'Jetson UART')]

    # Deferred so that espresso.py, which shells out to esptool, needs no pyserial for its
    # pin-only commands or on a Jetson -- where the root interpreter cannot see a --user install.
    try:
        from serial.tools import list_ports  # pylint: disable=import-outside-toplevel
    except ImportError as error:
        raise RuntimeError('Module pyserial is required to detect the serial device, but it is not '
                           'installed for this interpreter (e.g. "pip install pyserial"). Under sudo '
                           'that is the root interpreter, which sees neither a --user nor a virtualenv '
                           'install; passing the device path explicitly skips the detection.') from error

    devices: List[Device] = []
    seen: Set[str] = set()

    def add(path: str, description: str = '') -> None:
        # By real path: on macOS pyserial and EXTRA_PATTERNS report the same call-out node.
        real = os.path.realpath(path)
        if real not in seen:
            seen.add(real)
            devices.append(Device(path, description))

    for port in sorted(list_ports.comports(), key=lambda port: port.device):
        if port.vid is None:
            continue
        description = port.description or ''
        if description in ('', 'n/a'):  # pyserial's placeholder for a missing product name
            description = port.manufacturer or ''
        add(port.device, description)
    for pattern in EXTRA_PATTERNS:
        for path in sorted(glob.glob(pattern)):
            add(path)
    return devices


def resolve_device(path: Optional[str]) -> Device:
    """Return the device for an explicitly given path, or the detected one when there is none."""
    return Device(path) if path else choose_device()


def choose_device(*, ask: bool = True, allow_missing: bool = False) -> Device:
    """Return the serial device to talk to, asking which one when several are attached.

    The first candidate is not necessarily the Lizard -- a CDC-ACM gadget sorts ahead of a
    /dev/ttyUSB0 bridge -- and the callers go on to flash whatever comes back, so the question
    has no default and one that cannot be answered raises with the candidates named.

    Both keyword arguments exist for espresso.py's dry run, which prints a device it never
    opens: ``ask=False`` keeps it off stdin where there is no terminal to ask at, and
    ``allow_missing`` lets an empty or ambiguous bench yield a placeholder to print instead of
    an error. Neither turns into a guess: a caller that opens the port wants neither.
    """
    devices = find_devices()
    if not devices:
        if allow_missing:
            return Device(NO_DEVICE)
        raise RuntimeError('No serial device found')
    if len(devices) == 1:
        return devices[0]
    if not ask:
        if allow_missing:
            return Device(AMBIGUOUS_DEVICE)
        raise RuntimeError(cannot_ask_message(devices))
    print('Multiple serial devices found:')
    for i, device in enumerate(devices):
        print(f'  [{i}] {device}')
    while True:
        try:
            choice = input(f'Select device [0-{len(devices) - 1}]: ').strip()
        except EOFError:
            print()
            raise RuntimeError(cannot_ask_message(devices)) from None
        except KeyboardInterrupt:
            # Not the same situation as EOF: the question was answerable, the user declined to
            # answer it. Advising an explicit path would address a problem they do not have.
            print()
            raise SystemExit(130) from None
        # isdecimal instead of isdigit: the latter also accepts e.g. "²", which int() then rejects
        if choice.isdecimal() and int(choice) < len(devices):
            return devices[int(choice)]
        print('Invalid selection.')


def cannot_ask_message(devices: List[Device]) -> str:
    return ('Cannot ask which of several serial devices to use: '
            f'{", ".join(str(device) for device in devices)}. '
            'Pass the device path explicitly.')
