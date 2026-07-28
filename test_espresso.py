"""Tests for espresso.py's command wiring.

Only the commands that open the serial port may resolve a device, because resolving one can ask
which of several attached adapters to use -- a question the pin-only commands would block on
and, without a terminal, fail on, for an answer their handlers never read. Nothing about those
handlers looks different from main(), so the distinction has to be asserted from the outside.
"""
import subprocess
import sys
from contextlib import contextmanager
from dataclasses import replace
from pathlib import Path
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


@pytest.mark.parametrize('command', PIN_ONLY_COMMANDS + SERIAL_COMMANDS)
def test_a_remote_run_resolves_no_local_device(command: str) -> None:
    """Under --host the remote machine resolves its own device, so this one must not ask at all.

    A device resolved here would name the wrong machine's adapter, and asking would stop a
    remote flash on a question about the local bench. remote_command() passes the user's tokens
    through verbatim, so a leaked --device would go on to flash the remote at that path.
    """
    with attached(*TWO_BRIDGES), never_asked(), mock.patch.object(espresso, 'run_remote') as run_remote:
        espresso.main([command, '--host', 'user@host'])
    host, argv = run_remote.call_args.args
    assert host == 'user@host'
    assert argv == [command]


# Blocks pyserial the way a machine without it does, then imports the tooling that has to
# survive that: espresso.py shells out to esptool instead of importing it, so its own
# interpreter -- root's, under sudo, where a "pip install --user" is invisible -- never had to
# have pyserial. find_devices() is expected to fail, proving the import is deferred, not gone.
IMPORT_WITHOUT_PYSERIAL = '''
import sys

class Blocked:
    def find_spec(self, name, path=None, target=None):
        if name == 'serial' or name.startswith('serial.'):
            raise ImportError(f'No module named {name!r}')
        return None

sys.meta_path.insert(0, Blocked())

import serial_devices
import espresso

assert espresso.choose_device is serial_devices.choose_device

if serial_devices.IS_JETSON:
    # A Robot Brain resolves its UART without enumerating anything, so it needs no pyserial.
    assert serial_devices.find_devices() == [serial_devices.Device(serial_devices.jetson_uart(), 'Jetson UART')]
else:
    assert serial_devices.jetson_uart() is None
    try:
        serial_devices.find_devices()
    except ImportError:
        pass
    else:
        raise AssertionError('find_devices() enumerated without pyserial')
'''


def test_the_host_tools_import_without_pyserial() -> None:
    """Only the code that enumerates USB devices may need pyserial, not the act of importing.

    Runs in a subprocess because the block has to be in place before the first import, and this
    test module itself imports pyserial through conftest.
    """
    process = subprocess.run([sys.executable, '-c', IMPORT_WITHOUT_PYSERIAL],
                             cwd=Path(__file__).resolve().parent,
                             capture_output=True, text=True, check=False)
    assert process.returncode == 0, process.stderr
