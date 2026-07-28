#!/usr/bin/env python3
"""Serial device discovery shared by the Lizard host tools.

All host tools have to agree on which device they talk to, but the answer depends on the
machine: a Jetson Robot Brain reaches the microcontroller over a fixed platform UART, which
is not part of any USB enumeration and therefore has to be derived from the L4T version,
while a development host uses a USB-UART bridge that pyserial can enumerate -- including a
human-readable description that tells two attached bridges apart.
"""
import glob
import os
import re
import sys
from pathlib import Path
from typing import List, NamedTuple, Optional, Set

from serial.tools import list_ports

TEGRA_RELEASE = Path('/etc/nv_tegra_release')
IS_JETSON = TEGRA_RELEASE.exists()

# Where the Jetson's UART to the microcontroller sits, by L4T (Linux for Tegra) major version.
JETSON_UARTS = {35: '/dev/ttyTHS0', 36: '/dev/ttyTHS1'}

# USB-serial nodes pyserial's enumeration can miss, e.g. when a macOS vendor driver (Silicon
# Labs VCP, CH34x) creates a /dev node without exposing full USB metadata. Only the macOS
# call-out nodes ("cu.") are listed: opening the matching dial-in node ("tty.") can block
# until a carrier signal appears, and since both are separate paths they would otherwise
# turn one physical adapter into two candidates.
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
    """Return the Jetson's UART to the microcontroller, or None if this machine is not a Jetson."""
    if not IS_JETSON:
        return None
    match = re.search(r'R(\d+)', TEGRA_RELEASE.read_text(encoding='utf-8'))
    if not match:
        return None
    major = int(match.group(1))
    if major not in JETSON_UARTS:
        raise RuntimeError(f'Unsupported L4T (Linux for Tegra) version: {major}')
    return JETSON_UARTS[major]


def find_devices() -> List[Device]:
    """Return the serial devices that could be a microcontroller, best candidate first.

    On a Jetson that is exactly the platform UART: its USB ports are for peripherals, and
    pyserial cannot see the UART anyway. On any other machine only USB devices qualify --
    built-in and virtual ports (`/dev/ttyS*`, Bluetooth) are skipped, because a Lizard always
    sits behind a USB-UART bridge there and listing them would turn an unambiguous match into
    a pointless question. Duplicates are collapsed by their real path, so a stable
    `/dev/serial/by-id/...` symlink and its `/dev/ttyUSB*` target count as one device.
    """
    uart = jetson_uart()
    if uart is not None:
        return [Device(uart, 'Jetson UART')]

    devices: List[Device] = []
    seen: Set[str] = set()

    def add(path: str, description: str = '') -> None:
        real = os.path.realpath(path)
        if real not in seen:
            seen.add(real)
            devices.append(Device(path, description))

    for port in sorted(list_ports.comports(), key=lambda port: port.device):
        if port.vid is None:
            continue
        description = port.description or ''
        if description in ('', 'n/a'):  # pyserial's placeholder when the kernel reports no product name
            description = port.manufacturer or ''
        add(port.device, description)
    for pattern in EXTRA_PATTERNS:
        for path in sorted(glob.glob(pattern)):
            add(path)
    return devices


def resolve_default_device() -> str:
    """Return the default serial device for the machine actually running the command.

    Never asks and never fails: with nothing attached it names the conventional path for the
    platform, so a failure surfaces from the connection attempt rather than from the lookup --
    which keeps e.g. a dry run working on a machine that has no adapter plugged in.
    """
    devices = find_devices()
    if devices:
        return devices[0].path
    return '/dev/ttyUSB0' if sys.platform.startswith('linux') else '/dev/cu.SLAB_USBtoUART'


def choose_device() -> str:
    """Return the device to talk to, asking which one when several candidates are attached.

    Raises a RuntimeError when there is nothing to connect to, or when the answer cannot be
    read (no terminal, Ctrl+C) -- pass the device path explicitly in that case.
    """
    devices = find_devices()
    if not devices:
        raise RuntimeError('No serial device found')
    if len(devices) == 1:
        return devices[0].path
    print('Multiple serial devices found:')
    for i, device in enumerate(devices):
        print(f'  [{i}] {device}')
    while True:
        try:
            choice = input(f'Select device [0-{len(devices) - 1}, default 0]: ').strip()
        except (EOFError, KeyboardInterrupt):
            print()
            raise RuntimeError('No device selected; pass the device path explicitly, '
                               f'e.g. {devices[0].path}') from None
        if not choice:
            return devices[0].path
        # isdecimal instead of isdigit: the latter also accepts e.g. "²", which int() then rejects
        if choice.isdecimal() and int(choice) < len(devices):
            return devices[int(choice)].path
        print('Invalid selection.')
