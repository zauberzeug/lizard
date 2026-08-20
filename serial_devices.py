"""Serial device discovery shared by the Lizard host tools.

A Jetson Robot Brain reaches the microcontroller over a fixed platform UART that no
enumeration can see, so it has to be derived from the L4T version; a development host uses a
USB-UART bridge whose device node matches one of a few well-known patterns.
"""
import functools
import glob
import re
import sys
from pathlib import Path
from typing import List, Optional

TEGRA_RELEASE = Path('/etc/nv_tegra_release')
IS_JETSON = TEGRA_RELEASE.exists()

JETSON_UARTS = {35: '/dev/ttyTHS0', 36: '/dev/ttyTHS1'}  # by L4T (Linux for Tegra) major version

# The nodes the USB-serial bridges we use actually create. Only the macOS call-out nodes ("cu."):
# opening the dial-in twin ("tty.") can block until a carrier appears, and listing both would turn
# one adapter into two candidates. Built-in and virtual ports never match, so an attached adapter
# stays a unique match; --device covers adapters whose node matches no pattern.
PATTERNS = [
    '/dev/ttyUSB*',
    '/dev/ttyACM*',
    '/dev/cu.usbserial*',
    '/dev/cu.usbmodem*',
    '/dev/cu.wchusbserial*',
    '/dev/cu.SLAB_USBtoUART*',
]


def jetson_uart() -> Optional[str]:
    """Return the Jetson's UART to the microcontroller, or None when not running on a Jetson.

    An unreadable L4T version raises instead of falling back to the USB patterns, which on a
    Jetson match no microcontroller and would hide the real cause behind a missing-device error.
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


def find_devices() -> List[str]:
    """Return the serial devices that could be a microcontroller."""
    uart = jetson_uart()
    if uart is not None:
        return [uart]
    return sorted(path for pattern in PATTERNS for path in glob.glob(pattern))


@functools.lru_cache(maxsize=None)  # so a command reading the device twice asks at most once
def resolve_device(path: Optional[str] = None) -> str:
    """Return the device for an explicitly given path, or the detected one when there is none.

    The first candidate is not necessarily the Lizard -- a CDC-ACM gadget sorts ahead of a
    /dev/ttyUSB0 bridge -- and the callers go on to flash whatever comes back, so the question
    has no default and one that cannot be answered raises with the candidates named.
    """
    if path:
        return path
    devices = find_devices()
    if not devices:
        raise RuntimeError('No serial device found')
    if len(devices) == 1:
        return devices[0]
    if not sys.stdin.isatty():
        raise RuntimeError(cannot_ask_message(devices))
    # The whole exchange goes to stderr: a caller whose stdout is redirected (`monitor.py > log`,
    # `espresso.py -d | grep`) would otherwise wait at a prompt it cannot show.
    print('Multiple serial devices found:', file=sys.stderr)
    for i, device in enumerate(devices):
        print(f'  [{i}] {device}', file=sys.stderr)
    while True:
        try:
            print(f'Select device [0-{len(devices) - 1}]: ', end='', file=sys.stderr, flush=True)
            choice = input().strip()
        except EOFError:
            print(file=sys.stderr)
            raise RuntimeError(cannot_ask_message(devices)) from None
        except KeyboardInterrupt:
            # Not the same situation as EOF: the question was answerable, the user declined to
            # answer it. Advising an explicit path would address a problem they do not have.
            print(file=sys.stderr)
            raise SystemExit(130) from None
        # isdecimal instead of isdigit: the latter also accepts e.g. "²", which int() then rejects
        if choice.isdecimal() and int(choice) < len(devices):
            return devices[int(choice)]
        print('Invalid selection.', file=sys.stderr)


def cannot_ask_message(devices: List[str]) -> str:
    return (f'Cannot ask which of several serial devices to use: {", ".join(devices)}. '
            'Pass the device path explicitly.')
