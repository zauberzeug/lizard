#!/usr/bin/env python3
import subprocess
import sys

import argparse
from typing import Any, Generator, Tuple
import time
import gpiod
from contextlib import contextmanager

parser = argparse.ArgumentParser(description='Flash and control an ESP32 microcontroller from a Jetson board')

parser.add_argument('command', choices=['flash', 'enable', 'disable', 'reset', 'erase', 'release_pins'],
                    help='Command to execute')
parser.add_argument('-j', '--jetson', choices=['nano', 'xavier', 'orin',
                    'super'], default=None, help='Jetson board type')
parser.add_argument('--nand', action='store_true', help='Board has NAND gates')
parser.add_argument('--swap_pins', action='store_true',
                    help='Swap EN and G0 pins (for Jetson Orin with piggyback older than V0.5)')
parser.add_argument('--bootloader', default='build/bootloader/bootloader.bin', help='Path to bootloader')
parser.add_argument('--partition-table', default='build/partition_table/partition-table.bin',
                    help='Path to partition table')
parser.add_argument('--firmware', default='build/lizard.bin', help='Path to firmware binary')
parser.add_argument('--chip', choices=['esp32', 'esp32s3'], default='esp32', help='ESP chip type')
parser.add_argument('-d', '--dry-run', action='store_true', help='Dry run')
parser.add_argument('device', nargs='?', default='/dev/tty.SLAB_USBtoUART',
                    help='Serial device path (overwritten by --jetson)')

args = parser.parse_args()

EN_PIN = {'nano': 216, 'xavier': 436, 'orin': 460, 'super': 112}.get(args.jetson, -1)
G0_PIN = {'nano': 50, 'xavier': 428, 'orin': 492, 'super': 148}.get(args.jetson, -1)
GPIO_EN = 'PR.04' if args.jetson in ('orin', 'super') else f'gpio{EN_PIN}'
GPIO_G0 = 'PAC.06' if args.jetson in ('orin', 'super') else f'gpio{G0_PIN}'
if args.swap_pins:
    GPIO_EN, GPIO_G0 = GPIO_G0, GPIO_EN
DEVICE = {
    'nano': '/dev/ttyTHS1',
    'xavier': '/dev/ttyTHS0',
    'orin': '/dev/ttyTHS0',
    'super': '/dev/ttyTHS1',
}.get(args.jetson, args.device)
ON = 1 if args.nand else 0
OFF = 0 if args.nand else 1
DRY_RUN = args.dry_run
FLASH_FREQ = {'esp32': '40m', 'esp32s3': '80m'}.get(args.chip, '40m')
BOOTLOADER_OFFSET = {'esp32': '0x1000', 'esp32s3': '0x0'}.get(args.chip, '0x1000')


_chip: gpiod.Chip | None = None
_line_en: gpiod.Line | None = None
_line_g0: gpiod.Line | None = None


def _get_chip_and_lines() -> Tuple[gpiod.Chip, Any, Any]:
    """Get the GPIO chip and lines for EN and G0 pins."""
    chip = gpiod.Chip('gpiochip0')
    line_en = chip.find_line(GPIO_EN)
    line_g0 = chip.find_line(GPIO_G0)
    return (chip, line_en, line_g0)


@contextmanager
def _pin_config() -> Generator[None, None, None]:
    """Configure the EN and G0 pins to control the microcontroller."""
    global _chip, _line_en, _line_g0
    if not args.jetson:
        yield
        return
    print_bold('Configuring EN and G0 pins...')
    _chip, _line_en, _line_g0 = _get_chip_and_lines()
    _line_en.request(consumer='espresso', type=gpiod.LINE_REQ_DIR_OUT)
    _line_g0.request(consumer='espresso', type=gpiod.LINE_REQ_DIR_OUT)
    time.sleep(0.5)
    yield
    _release_pins()


def _release_pins() -> None:
    """Release pins."""
    global _line_en, _line_g0
    if _line_en is not None:
        _line_en.release()
        _line_en = None
    if _line_g0 is not None:
        _line_g0.release()
        _line_g0 = None


@contextmanager
def _flash_mode() -> Generator[None, None, None]:
    """Bring the microcontroller into flash mode."""
    if not args.jetson:
        yield
        return
    print_bold('Bringing the microcontroller into flash mode...')
    set_gpio_value(_line_en, ON)
    time.sleep(0.5)
    set_gpio_value(_line_g0, ON)
    time.sleep(0.5)
    set_gpio_value(_line_en, OFF)
    time.sleep(0.5)
    yield
    _reset()


def enable() -> None:
    """Enable the microcontroller."""
    print_bold('Enabling the microcontroller...')
    with _pin_config():
        set_gpio_value(_line_g0, OFF)
        time.sleep(0.5)
        set_gpio_value(_line_en, OFF)


def disable() -> None:
    """Disable the microcontroller."""
    print_bold('Disabling the microcontroller...')
    with _pin_config():
        set_gpio_value(_line_en, ON)


def reset() -> None:
    """Reset the microcontroller."""
    print_bold('Resetting the microcontroller...')
    with _pin_config():
        _reset()


def _reset() -> None:
    """Set pins to reset the microcontroller."""
    set_gpio_value(_line_g0, OFF)
    time.sleep(0.5)
    set_gpio_value(_line_en, ON)
    time.sleep(0.5)
    set_gpio_value(_line_en, OFF)


def erase() -> None:
    """Erase the microcontroller."""
    print_bold('Erasing the microcontroller...')
    with _pin_config():
        with _flash_mode():
            success = run(
                'esptool.py',
                '--chip', args.chip,
                '--port', DEVICE,
                '--baud', '921600',
                '--before', 'default_reset',
                '--after', 'hard_reset',
                'erase_flash',
            )
            if not success:
                raise RuntimeError('Failed to erase flash.')


def flash() -> None:
    """Flash the microcontroller."""
    print_bold('Flashing...')
    with _pin_config():
        with _flash_mode():
            success = run(
                'esptool.py',
                '--chip', args.chip,
                '--port', DEVICE,
                '--baud', '921600',
                '--before', 'default_reset',
                '--after', 'hard_reset',
                'write_flash',
                '-z',
                '--flash_mode', 'dio',
                '--flash_freq', FLASH_FREQ,
                '--flash_size', 'detect',
                BOOTLOADER_OFFSET, args.bootloader,
                '0x8000', args.partition_table,
                '0x20000', args.firmware,
            )
            if not success:
                raise RuntimeError('Flashing failed. Use "sudo" and check your parameters.')


def run(*run_args: str) -> bool:
    """Run a command and return whether it was successful."""
    print(f'  {" ".join(run_args)}')
    if DRY_RUN:
        return True
    result = subprocess.run(run_args, check=False)
    return result.returncode == 0


def set_gpio_value(line: gpiod.Line | None, value: int) -> None:
    """Set a GPIO line value."""
    if line is None:
        return
    pin_name = 'EN' if line == _line_en else 'G0'
    print(f'set {pin_name} pin to {value}')
    if DRY_RUN:
        return
    line.set_value(value)


def print_ok(message: str) -> None:
    print_bold(f'\033[94m{message}\033[0m')


def print_bold(message: str) -> None:
    print(f'\033[1m{message}\033[0m')


def print_fail(message: str) -> None:
    print(f'\033[91m{message}\033[0m')


def main(command: str) -> None:
    print_ok('Espresso dry-running...' if DRY_RUN else 'Espresso running...')
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
