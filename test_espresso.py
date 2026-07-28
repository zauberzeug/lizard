"""Tests for espresso.py's command wiring.

Only the commands that open the serial port may resolve a device, because resolving one can ask
which of several attached adapters to use -- a question the pin-only commands would block on
and, without a terminal, fail on, for an answer their handlers never read. Nothing about those
handlers looks different from main(), so the distinction has to be asserted from the outside.
"""
from contextlib import contextmanager
from dataclasses import replace
from typing import Dict, Iterator, List
from unittest import mock

import pytest

import espresso
from conftest import TWO_BRIDGES, answers, attached, never_asked

# Commands that only toggle the EN/G0 pins, and hence never look at config.device.
PIN_ONLY_COMMANDS: List[str] = ['enable', 'disable', 'reset', 'release_pins']

# Commands that pass config.device on to esptool or esp_coredump.
SERIAL_COMMANDS: List[str] = ['flash', 'erase', 'coredump']


@contextmanager
def recorded(command: str) -> Iterator[Dict[str, espresso.Config]]:
    """Run the command with a handler that only records the Config main() built for it.

    GPIOD_VERSION and IS_JETSON are pinned so the run takes the non-Jetson fallback path
    regardless of the machine the tests happen to run on.
    """
    captured: Dict[str, espresso.Config] = {}
    stub = replace(espresso.COMMANDS[command], handler=lambda config: captured.update(config=config))
    with mock.patch.dict(espresso.COMMANDS, {command: stub}), \
            mock.patch.object(espresso, 'GPIOD_VERSION', None), \
            mock.patch.object(espresso, 'IS_JETSON', False):
        yield captured


@pytest.mark.parametrize('command', PIN_ONLY_COMMANDS)
def test_pin_only_commands_do_not_resolve_a_device(command: str) -> None:
    """Asking here would block a scripted pin toggle on an answer that is then discarded."""
    with attached(*TWO_BRIDGES), never_asked(), recorded(command) as captured:
        espresso.main([command])
    assert captured['config'].device == ''


@pytest.mark.parametrize('command', SERIAL_COMMANDS)
def test_serial_commands_resolve_the_chosen_device(command: str) -> None:
    """The answer has to reach the command that opens the port, not just the prompt."""
    with attached(*TWO_BRIDGES), answers('1'), recorded(command) as captured:
        espresso.main([command])
    assert captured['config'].device == '/dev/ttyUSB1'


@pytest.mark.parametrize('command', SERIAL_COMMANDS)
def test_an_explicit_device_is_never_second_guessed(command: str) -> None:
    with attached(*TWO_BRIDGES), never_asked(), recorded(command) as captured:
        espresso.main([command, '--device', '/dev/ttyUSB9'])
    assert captured['config'].device == '/dev/ttyUSB9'


def test_a_dry_run_resolves_without_a_question() -> None:
    """It prints the device but never opens it, so it must not block on stdin either."""
    with attached(*TWO_BRIDGES), never_asked(), recorded('flash') as captured:
        espresso.main(['flash', '--dry-run'])
    assert captured['config'].device == '/dev/ttyUSB0'


def test_every_command_declares_whether_it_opens_the_port() -> None:
    """A new command has to join one of the lists above, so the question above gets answered."""
    assert sorted(PIN_ONLY_COMMANDS + SERIAL_COMMANDS) == sorted(espresso.COMMANDS)
