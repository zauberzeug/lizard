"""Test helpers shared by the host tool tests.

Both test modules have to pretend that a particular set of serial devices is attached to a
machine that is not a Jetson, so the mocking of pyserial's enumeration lives here rather than
in whichever module happened to need it first.
"""
import builtins
from contextlib import contextmanager
from typing import Iterator, Optional, Sequence
from unittest import mock

from serial.tools import list_ports
from serial.tools.list_ports_common import ListPortInfo

import serial_devices as sd


def port(device: str, *, vid: Optional[int] = 0x10c4, description: str = 'n/a',
         manufacturer: str = '') -> ListPortInfo:
    """Build the port info pyserial would report for an attached device."""
    info = ListPortInfo(device)
    info.vid = vid
    info.description = description
    info.manufacturer = manufacturer
    return info


# An ambiguous bench: two adapters, so anything that resolves a device has to ask which one.
TWO_BRIDGES = (port('/dev/ttyUSB0', description='first'), port('/dev/ttyUSB1', description='second'))


@contextmanager
def attached(*ports: ListPortInfo, patterns: Sequence[str] = ()) -> Iterator[None]:
    """Pretend the given ports are attached to a host that is not a Jetson.

    comports() is patched on pyserial itself rather than on serial_devices, which imports it
    inside find_devices() so that importing the module needs no pyserial at all.
    """
    with mock.patch.multiple(sd, EXTRA_PATTERNS=list(patterns), IS_JETSON=False), \
            mock.patch.object(list_ports, 'comports', return_value=list(ports)):
        yield


@contextmanager
def answers(*replies: str) -> Iterator[None]:
    """Answer the device question with the given replies, one per prompt."""
    with mock.patch.object(builtins, 'input', side_effect=list(replies)):
        yield


@contextmanager
def never_asked() -> Iterator[None]:
    """Fail the test if the device question is asked at all."""
    with mock.patch.object(builtins, 'input', side_effect=AssertionError('asked for a device')):
        yield
