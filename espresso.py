#!/usr/bin/env python3
import subprocess
import sys

import argparse
from typing import Generator, Union
import time
from pathlib import Path
from contextlib import contextmanager

parser = argparse.ArgumentParser(description='Flash and control an ESP32 microcontroller from a Jetson board')

parser.add_argument('command', choices=['flash', 'enable', 'disable', 'reset', 'erase'], help='Command to execute')
parser.add_argument('-j', '--jetson', choices=['nano', 'xavier', 'orin'], default=None, help='Jetson board type')
parser.add_argument('--nand', action='store_true', help='Board has NAND gates')
parser.add_argument('--swap_pins', action='store_true',
                    help='Swap EN and G0 pins (for Jetson Orin with piggyback older than V0.5)')
parser.add_argument('--bootloader', default='build/bootloader/bootloader.bin', help='Path to bootloader')
parser.add_argument('--partition-table', default='build/partition_table/partition-table.bin',
                    help='Path to partition table')
parser.add_argument('--firmware', default='build/lizard.bin', help='Path to firmware binary')
parser.add_argument('-d', '--dry-run', action='store_true', help='Dry run')
parser.add_argument('device', nargs='?', default='/dev/tty.SLAB_USBtoUART',
                    help='Serial device path (overwritten by --jetson)')

args = parser.parse_args()

EN_PIN = {'nano': 216, 'xavier': 436, 'orin': 460}.get(args.jetson, -1)
G0_PIN = {'nano': 50, 'xavier': 428, 'orin': 492}.get(args.jetson, -1)
GPIO_EN = 'PR.04' if args.jetson == 'orin' else f'gpio{EN_PIN}'
GPIO_G0 = 'PAC.06' if args.jetson == 'orin' else f'gpio{G0_PIN}'
if args.swap_pins:
    EN_PIN, G0_PIN = G0_PIN, EN_PIN
    GPIO_EN, GPIO_G0 = GPIO_G0, GPIO_EN
DEVICE = {
    'nano': '/dev/ttyTHS1',
    'xavier': '/dev/ttyTHS0',
    'orin': '/dev/ttyTHS0',
}.get(args.jetson, args.device)
ON = 1 if args.nand else 0
OFF = 0 if args.nand else 1
DRY_RUN = args.dry_run


@contextmanager
def pin_config() -> Generator[None, None, None]:
    """Configure the EN and G0 pins to control the microcontroller."""
    if not args.jetson:
        yield
        return
    print_bold('Configuring EN and G0 pins...')
    write_gpio('export', EN_PIN)
    time.sleep(0.5)
    write_gpio('export', G0_PIN)
    time.sleep(0.5)
    write_gpio(f'{GPIO_EN}/direction', 'out')
    time.sleep(0.5)
    write_gpio(f'{GPIO_G0}/direction', 'out')
    time.sleep(0.5)
    yield
    write_gpio('unexport', EN_PIN)
    time.sleep(0.5)
    write_gpio('unexport', G0_PIN)


@contextmanager
def flash_mode() -> Generator[None, None, None]:
    """Bring the microcontroller into flash mode."""
    if not args.jetson:
        yield
        return
    print_bold('Bringing the microcontroller into flash mode...')
    write_gpio(f'{GPIO_EN}/value', ON)
    time.sleep(0.5)
    write_gpio(f'{GPIO_G0}/value', ON)
    time.sleep(0.5)
    write_gpio(f'{GPIO_EN}/value', OFF)
    time.sleep(0.5)
    yield
    reset()


def enable() -> None:
    """Enable the microcontroller."""
    print_bold('Enabling the microcontroller...')
    with pin_config():
        write_gpio(f'{GPIO_G0}/value', OFF)
        time.sleep(0.5)
        write_gpio(f'{GPIO_EN}/value', OFF)


def disable() -> None:
    """Disable the microcontroller."""
    print_bold('Disabling the microcontroller...')
    with pin_config():
        write_gpio(f'{GPIO_EN}/value', ON)


def reset() -> None:
    """Reset the microcontroller."""
    print_bold('Resetting the microcontroller...')
    with pin_config():
        write_gpio(f'{GPIO_G0}/value', OFF)
        time.sleep(0.5)
        write_gpio(f'{GPIO_EN}/value', ON)
        time.sleep(0.5)
        write_gpio(f'{GPIO_EN}/value', OFF)


def erase() -> None:
    """Erase the microcontroller."""
    print_bold('Erasing the microcontroller...')
    with pin_config():
        with flash_mode():
            success = run(
                'esptool.py',
                '--chip', 'esp32',
                '--port', DEVICE,
                '--baud', '921600',
                '--before', 'default_reset',
                '--after', 'hard_reset',
                'erase_flash',
            )
            if not success:
                print('Failed to erase flash.')
                sys.exit(1)


def flash() -> None:
    """Flash the microcontroller."""
    print_bold('Flashing...')
    with pin_config():
        with flash_mode():
            success = run(
                'esptool.py',
                '--chip', 'esp32',
                '--port', DEVICE,
                '--baud', '921600',
                '--before', 'default_reset',
                '--after', 'hard_reset',
                'write_flash',
                '-z',
                '--flash_mode', 'dio',
                '--flash_freq', '40m',
                '--flash_size', 'detect',
                '0x1000', args.bootloader,
                '0x8000', args.partition_table,
                '0x20000', args.firmware,
            )
            if not success:
                print_fail('Flashing failed. Maybe you need different parameters? Or you forgot "sudo"?\n')
                sys.exit(1)


def run(*run_args: str) -> bool:
    """Run a command and return whether it was successful."""
    print(f'  {" ".join(run_args)}')
    if DRY_RUN:
        return True
    result = subprocess.run(run_args, check=False)
    return result.returncode == 0


def write_gpio(path: str, value: Union[str, int]) -> None:
    """Write a value to a GPIO file."""
    print(f'  echo {value:3} > /sys/class/gpio/{path}')
    if DRY_RUN:
        return
    Path(f'/sys/class/gpio/{path}').write_text(f'{value}\n', encoding='utf-8')


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
    else:
        print(f'Invalid command "{command}".')
        sys.exit(1)
    print_ok('Finished. ☕️')


if __name__ == '__main__':
    main(args.command)
