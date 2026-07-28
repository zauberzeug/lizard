"""Tests for serial_devices.py.

No hardware required: pyserial's enumeration and /etc/nv_tegra_release are both mocked, which
is the only way to cover the machines this module exists for -- a Jetson Robot Brain and a
development host with several adapters attached -- from a single CI runner.
"""
import builtins
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator, Optional, Sequence
from unittest import mock

import pytest
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


@contextmanager
def attached(*ports: ListPortInfo, patterns: Sequence[str] = ()) -> Iterator[None]:
    """Pretend the given ports are attached to a host that is not a Jetson."""
    with mock.patch.multiple(sd, EXTRA_PATTERNS=list(patterns), IS_JETSON=False), \
            mock.patch.object(sd.list_ports, 'comports', return_value=list(ports)):
        yield


@contextmanager
def jetson(release: str) -> Iterator[None]:
    """Pretend to run on a Jetson whose /etc/nv_tegra_release holds the given content."""
    with mock.patch.object(sd, 'IS_JETSON', True), \
            mock.patch.object(sd, 'TEGRA_RELEASE', mock.Mock(spec=Path, read_text=lambda **_: release)):
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


TWO_BRIDGES = (port('/dev/ttyUSB0', description='first'), port('/dev/ttyUSB1', description='second'))


def test_non_usb_ports_are_skipped() -> None:
    """A built-in or virtual port must not turn an otherwise unambiguous match into a question."""
    with attached(port('/dev/ttyS0', vid=None), port('/dev/ttyUSB0', description='CP2102 USB to UART Bridge')):
        assert sd.find_devices() == [sd.Device('/dev/ttyUSB0', 'CP2102 USB to UART Bridge')]


def test_description_falls_back_to_the_manufacturer() -> None:
    """"n/a" is pyserial's placeholder, not something worth showing in the selection list."""
    with attached(port('/dev/ttyUSB0', description='n/a', manufacturer='Silicon Labs')):
        assert sd.find_devices() == [sd.Device('/dev/ttyUSB0', 'Silicon Labs')]


def test_native_usb_port_is_found() -> None:
    """An ESP32-S3 flashed over its own USB port enumerates as CDC-ACM rather than as a bridge."""
    with attached(port('/dev/ttyACM0', description='USB JTAG/serial debug unit')):
        assert sd.find_devices() == [sd.Device('/dev/ttyACM0', 'USB JTAG/serial debug unit')]


def test_glob_fallback_dedups_against_pyserial() -> None:
    """One adapter reported by both sources counts once, keeping the description pyserial had.

    /dev/null stands in for a real device node here because the glob needs a path that exists.
    """
    with attached(port('/dev/null', description='pyserial'), patterns=['/dev/nul?']):
        assert sd.find_devices() == [sd.Device('/dev/null', 'pyserial')]


def test_empty_answer_takes_the_first_device() -> None:
    with attached(*TWO_BRIDGES), answers(''):
        assert sd.choose_device() == '/dev/ttyUSB0'


def test_explicit_answer_is_honoured() -> None:
    with attached(*TWO_BRIDGES), answers('1'):
        assert sd.choose_device() == '/dev/ttyUSB1'


@pytest.mark.parametrize('invalid', ['9', 'x', '-1', '²'])
def test_invalid_answers_are_rejected(invalid: str) -> None:
    """"²" is why the check is isdecimal() and not isdigit(): int() would reject it afterwards."""
    with attached(*TWO_BRIDGES), answers(invalid, '1'):
        assert sd.choose_device() == '/dev/ttyUSB1'


def test_unanswerable_question_names_the_candidates() -> None:
    """A non-interactive run must fail loudly instead of writing to whichever board sorts first."""
    with attached(*TWO_BRIDGES), mock.patch.object(builtins, 'input', side_effect=EOFError):
        with pytest.raises(RuntimeError, match='Pass the device path explicitly'):
            sd.choose_device()


def test_ask_false_resolves_without_a_question() -> None:
    """A dry run prints a device but never opens it, so it must not block on stdin."""
    with attached(*TWO_BRIDGES), never_asked():
        assert sd.choose_device(ask=False) == '/dev/ttyUSB0'


def test_missing_device_raises() -> None:
    with attached():
        with pytest.raises(RuntimeError, match='No serial device found'):
            sd.choose_device()


def test_missing_device_may_fall_back_to_the_conventional_path() -> None:
    """So the failure surfaces from the connection attempt rather than from the lookup."""
    with attached():
        assert sd.choose_device(allow_missing=True) == sd.FALLBACK_DEVICE


@pytest.mark.parametrize(('release', 'expected'), [('# R35 (release), REVISION: 4.1\n', '/dev/ttyTHS0'),
                                                   ('# R36 (release), REVISION: 3.0\n', '/dev/ttyTHS1')])
def test_l4t_version_decides_the_jetson_uart(release: str, expected: str) -> None:
    """The UART moved between L4T 35 and 36, and it is the only candidate on a Jetson."""
    with jetson(release), never_asked():
        assert sd.find_devices() == [sd.Device(expected, 'Jetson UART')]
        assert sd.choose_device() == expected


def test_unsupported_l4t_version_raises() -> None:
    with jetson('# R32 (release), REVISION: 7.1\n'):
        with pytest.raises(RuntimeError, match='Unsupported L4T'):
            sd.find_devices()


def test_unreadable_l4t_version_raises() -> None:
    """Falling back to USB enumeration on a Jetson would hide the real cause."""
    with jetson('something unexpected\n'):
        with pytest.raises(RuntimeError, match='Cannot determine the L4T'):
            sd.find_devices()
