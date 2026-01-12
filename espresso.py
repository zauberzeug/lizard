#!/usr/bin/env python3
import re
import subprocess
import sys
import time
from contextlib import contextmanager
from typing import Generator, Optional
from pathlib import Path
import argparse
try:
    import gpiod
except ImportError:
    gpiod = None  # type: ignore[assignment]
    print('gpiod module not found. Please install it using "sudo apt install python3-gpiod". Ignore this error if you are not on a Jetson board.')


JETPACK: Optional[int] = None
path = Path('/etc/nv_tegra_release')
if path.exists() and (match := re.search(r'R(\d+)', path.read_text(encoding='utf-8'))):
    major = int(match.group(1))
    if major >= 36:
        JETPACK = 6
    elif major >= 35:
        JETPACK = 5
DEFAULT_DEVICE = {
    5: '/dev/ttyTHS0',
    6: '/dev/ttyTHS1',
    None: '/dev/tty.SLAB_USBtoUART',
}

parser = argparse.ArgumentParser(description='Flash and control an ESP32 microcontroller from a Jetson board')

parser.add_argument('command', choices=['flash', 'enable', 'disable', 'reset', 'erase', 'release_pins'],
                    help='Command to execute')
parser.add_argument('--nand', action='store_true', help='Board has NAND gates')
parser.add_argument('--bootloader', default='build/bootloader/bootloader.bin', help='Path to bootloader')
parser.add_argument('--partition-table', default='build/partition_table/partition-table.bin',
                    help='Path to partition table')
parser.add_argument('--firmware', default='build/lizard.bin', help='Path to firmware binary')
parser.add_argument('--chip', choices=['esp32', 'esp32s3'], default='esp32', help='ESP chip type')
parser.add_argument('-d', '--dry-run', action='store_true', help='Dry run')
parser.add_argument('--device', nargs='?', default=DEFAULT_DEVICE.get(
    JETPACK), help='Serial device path (auto-detected on Jetson)')

args = parser.parse_args()
ON = 1 if args.nand else 0
OFF = 0 if args.nand else 1
DRY_RUN = args.dry_run
CHIP = args.chip
DEVICE = args.device
FLASH_FREQ = {'esp32': '40m', 'esp32s3': '80m'}.get(CHIP, '40m')
BOOTLOADER_OFFSET = {'esp32': '0x1000', 'esp32s3': '0x0'}.get(CHIP, '0x1000')
BOOTLOADER = args.bootloader
PARTITION_TABLE = args.partition_table
FIRMWARE = args.firmware

if JETPACK:
    chip = gpiod.Chip('gpiochip0')
    en = chip.find_line('PR.04')
    g0 = chip.find_line('PAC.06')


@contextmanager
def _pin_config() -> Generator[None, None, None]:
    """Configure the EN and G0 pins to control the microcontroller."""
    if JETPACK:
        print_bold('Configuring EN and G0 pins...')
        en.request(consumer='espresso', type=gpiod.LINE_REQ_DIR_OUT)
        g0.request(consumer='espresso', type=gpiod.LINE_REQ_DIR_OUT)
        time.sleep(0.5)
    yield
    if JETPACK:
        _release_pins()


def _release_pins() -> None:
    """Release pins."""
    if JETPACK:
        en.release()
        g0.release()


@contextmanager
def _flash_mode() -> Generator[None, None, None]:
    """Bring the microcontroller into flash mode."""
    if JETPACK:
        print_bold('Bringing the microcontroller into flash mode...')
        set_en(ON)
        time.sleep(0.5)
        set_g0(ON)
        time.sleep(0.5)
        set_en(OFF)
        time.sleep(0.5)
    yield
    if JETPACK:
        _reset()


def enable() -> None:
    """Enable the microcontroller."""
    print_bold('Enabling the microcontroller...')
    with _pin_config():
        set_g0(OFF)
        time.sleep(0.5)
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
    time.sleep(0.5)
    set_en(ON)
    time.sleep(0.5)
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
        en.set_value(value)


def set_g0(value: int) -> None:
    print(f'  Setting G0 pin to {value}')
    if not DRY_RUN:
        g0.set_value(value)


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
