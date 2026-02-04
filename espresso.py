#!/usr/bin/env python3
import re
import subprocess
import sys
import time
from contextlib import contextmanager
from pathlib import Path
from typing import Generator
import argparse

try:
    import gpiod
except ImportError:
    GPIOD_VERSION = None
else:
    GPIOD_VERSION = 2 if hasattr(gpiod, 'request_lines') else 1


class GpioController:
    def __init__(self, en: str, g0: str) -> None:
        pass

    def request_outputs(self, consumer: str) -> None:
        """Request output lines."""

    def set_value(self, key: str, value: int) -> None:
        """Set the value of a line."""

    def release(self) -> None:
        """Release the lines."""


class GpioControllerV1(GpioController):

    def __init__(self, en: str, g0: str) -> None:
        chip = gpiod.Chip('gpiochip0')
        self._lines = {
            'en': chip.find_line(en),
            'g0': chip.find_line(g0),
        }

    def request_outputs(self, consumer: str) -> None:
        for line in self._lines.values():
            line.request(consumer=consumer, type=gpiod.LINE_REQ_DIR_OUT)

    def set_value(self, key: str, value: int) -> None:
        self._lines[key].set_value(value)

    def release(self) -> None:
        for line in self._lines.values():
            line.release()


class GpioControllerV2(GpioController):
    CHIP_PATH = '/dev/gpiochip0'

    def __init__(self, en: str, g0: str) -> None:
        chip = gpiod.Chip(self.CHIP_PATH)
        self._offsets = {
            'en': chip.line_offset_from_id(en),
            'g0': chip.line_offset_from_id(g0),
        }
        chip.close()
        self._request = None

    def request_outputs(self, consumer: str) -> None:
        from gpiod.line import Direction  # pylint: disable=import-outside-toplevel
        config = {offset: gpiod.LineSettings(direction=Direction.OUTPUT) for offset in self._offsets.values()}
        self._request = gpiod.request_lines(self.CHIP_PATH, consumer=consumer, config=config)

    def set_value(self, key: str, value: int) -> None:
        from gpiod.line import Value  # pylint: disable=import-outside-toplevel
        if self._request:
            self._request.set_value(self._offsets[key], Value.ACTIVE if value else Value.INACTIVE)

    def release(self) -> None:
        if self._request:
            self._request.release()
            self._request = None


DEFAULT_DEVICE = '/dev/tty.SLAB_USBtoUART'
path = Path('/etc/nv_tegra_release')
if path.exists() and (match := re.search(r'R(\d+)', path.read_text(encoding='utf-8'))):
    major = int(match.group(1))
    if major == 35:
        DEFAULT_DEVICE = '/dev/ttyTHS0'
    elif major == 36:
        DEFAULT_DEVICE = '/dev/ttyTHS1'
    else:
        raise RuntimeError(f'Unsupported L4T (Linux for Tegra) version: {major}')

parser = argparse.ArgumentParser(description='Flash and control an ESP32 microcontroller from a Jetson board')

parser.add_argument('command', choices=['flash', 'enable', 'disable', 'reset', 'erase', 'release_pins'],
                    help='Command to execute')
parser.add_argument('--nand', action='store_true', help='Board has NAND gates')
parser.add_argument('--bootloader', default='build/bootloader/bootloader.bin', help='Path to bootloader')
parser.add_argument('--partition-table', default='build/partition_table/partition-table.bin',
                    help='Path to partition table')
parser.add_argument('--swap', action='store_true', help='Swap En and G0 pins for piggyboard version lower than v0.5')
parser.add_argument('--firmware', default='build/lizard.bin', help='Path to firmware binary')
parser.add_argument('--chip', choices=['esp32', 'esp32s3'], default='esp32', help='ESP chip type')
parser.add_argument('--reset-partition', action='store_true', help='Reset to default OTA partition after flashing')
parser.add_argument('-d', '--dry-run', action='store_true', help='Dry run')
parser.add_argument('--device', nargs='?', default=DEFAULT_DEVICE, help='Serial device path (auto-detected on Jetson)')

args = parser.parse_args()
ON = 1 if args.nand else 0
OFF = 0 if args.nand else 1
DRY_RUN = args.dry_run
SWAP = args.swap
CHIP = args.chip
DEVICE = args.device
FLASH_FREQ = {'esp32': '40m', 'esp32s3': '80m'}.get(CHIP, '40m')
BOOTLOADER_OFFSET = {'esp32': '0x1000', 'esp32s3': '0x0'}.get(CHIP, '0x1000')
BOOTLOADER = args.bootloader
PARTITION_TABLE = args.partition_table
FIRMWARE = args.firmware

EN = 'PR.04'
G0 = 'PAC.06'
if SWAP:
    EN, G0 = G0, EN
gpio = {
    None: GpioController,
    1: GpioControllerV1,
    2: GpioControllerV2,
}[GPIOD_VERSION](EN, G0)


@contextmanager
def _pin_config() -> Generator[None, None, None]:
    """Configure the EN and G0 pins to control the microcontroller."""
    print_bold('Configuring EN and G0 pins...')
    gpio.request_outputs(consumer='espresso')
    time.sleep(0.5)
    yield
    _release_pins()


def _release_pins() -> None:
    """Release pins."""
    gpio.release()


@contextmanager
def _flash_mode() -> Generator[None, None, None]:
    """Bring the microcontroller into flash mode."""
    print_bold('Bringing the microcontroller into flash mode...')
    set_en(ON)
    set_g0(ON)
    set_en(OFF)
    yield
    _reset()


def enable() -> None:
    """Enable the microcontroller."""
    print_bold('Enabling the microcontroller...')
    with _pin_config():
        set_g0(OFF)
        set_en(OFF)


def disable() -> None:
    """Disable the microcontroller."""
    print_bold('Disabling the microcontroller...')
    with _pin_config():
        set_en(ON)


def reset() -> None:
    """Reset the microcontroller."""
    print_bold('Resetting the microcontroller...')
    with _pin_config():
        _reset()


def _reset() -> None:
    """Set pins to reset the microcontroller."""
    set_g0(OFF)
    set_en(ON)
    set_en(OFF)


def erase() -> None:
    """Erase the microcontroller."""
    print_bold('Erasing the microcontroller...')
    with _pin_config():
        with _flash_mode():
            success = run(
                'esptool.py',
                '--chip', CHIP,
                '--port', DEVICE,
                '--baud', '921600',
                '--before', 'default_reset',
                '--after', 'hard_reset',
                'erase_flash',
            )
            if not success:
                raise RuntimeError('Failed to erase flash.')


def reset_partition() -> None:
    """Reset the OTA partition to the default state."""
    print_bold('Resetting partition to "ota_0"...')
    success = run(
        'esptool.py',
        '--chip', args.chip,
        '--port', DEVICE,
        '--baud', '115200',
        'erase_region',
        '0xf000',
        '0x2000',
    )
    if not success:
        raise RuntimeError('Failed to reset OTA partition.')


def flash() -> None:
    """Flash the microcontroller."""
    print_bold('Flashing...')
    with _pin_config():
        with _flash_mode():
            success = run(
                'esptool.py',
                '--chip', CHIP,
                '--port', DEVICE,
                '--baud', '921600',
                '--before', 'default_reset',
                '--after', 'hard_reset',
                'write_flash',
                '-z',
                '--flash_mode', 'dio',
                '--flash_freq', FLASH_FREQ,
                '--flash_size', 'detect',
                BOOTLOADER_OFFSET, BOOTLOADER,
                '0x8000', PARTITION_TABLE,
                '0x20000', FIRMWARE,
            )
            if not success:
                raise RuntimeError('Flashing failed. Use "sudo" and check your parameters.')
            if args.reset_partition:
                reset_partition()


def run(*run_args: str) -> bool:
    """Run a command and return whether it was successful."""
    print(f'  {" ".join(run_args)}')
    if DRY_RUN:
        return True
    result = subprocess.run(run_args, check=False)
    return result.returncode == 0


def set_en(value: int) -> None:
    print(f'  Setting EN pin to {value}')
    if not DRY_RUN:
        gpio.set_value('en', value)
        time.sleep(0.5)


def set_g0(value: int) -> None:
    print(f'  Setting G0 pin to {value}')
    if not DRY_RUN:
        gpio.set_value('g0', value)
        time.sleep(0.5)


def print_ok(message: str) -> None:
    print_bold(f'\033[94m{message}\033[0m')


def print_bold(message: str) -> None:
    print(f'\033[1m{message}\033[0m')


def print_fail(message: str) -> None:
    print(f'\033[91m{message}\033[0m')


def main(command: str) -> None:
    print_ok('Espresso dry-running...' if DRY_RUN else 'Espresso running...')
    if GPIOD_VERSION is None and not DRY_RUN:
        print_fail('Module gpiod not found. Espresso is not able to control the EN and G0 pins.')
    if command == 'enable':
        enable()
    elif command == 'disable':
        disable()
    elif command == 'reset':
        reset()
    elif command == 'erase':
        erase()
    elif command == 'flash':
        flash()
    elif command == 'release_pins':
        _release_pins()
    else:
        raise RuntimeError(f'Invalid command "{command}".')
    print_ok('Finished. ☕️')


if __name__ == '__main__':
    try:
        main(args.command)
    except RuntimeError as e:
        print_fail(f'Error: {e}')
        sys.exit(1)
