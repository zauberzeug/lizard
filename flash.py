#!/usr/bin/env python3
import subprocess
import sys

from esp import Esp


def show_help() -> None:
    print(f'{sys.argv[0]} [nano | xavier | orin] [nand | v05] [usb | /dev/<name>] [enable] [-e | --erase] [reset]')
    print('   -e, --erase   erase the flash before flashing the new firmware')
    print('   nano          flashing Jetson Nano (default)')
    print('   xavier        flashing Jetson Xavier')
    print('   orin          flashing Jetson Orin')
    print('   nand          Robot Brain has piggyboard with NAND gate (eg. older version)')
    print('   v05           Robot Brain has piggyboard with V0.5 or higher (eg. newer version)')
    print('   usb           use /dev/tty.SLAB_USBtoUART as serial device')
    print('   /dev/<name>   use /dev/<name> as serial device')
    print('   enable        enable the ESP32 microcontroller')
    print('   reset         reset the ESP32 microcontroller')


print(
    '\033[93m\033[1m'
    'This script will be deprecated in version 1.0.0. '
    'Please consider using the new espresso.py script instead.'
    '\033[0m',
)

if any(h in sys.argv for h in ['--help', '-help', 'help', '-h']):
    show_help()
    sys.exit()

erase_flash = any(e in sys.argv for e in ['-e', '--erase'])
device = '/dev/tty.SLAB_USBtoUART' if 'usb' in sys.argv else None
for p in sys.argv:
    if p.startswith('/dev/'):
        device = p

esp = Esp(
    jetson='xavier' if 'xavier' in sys.argv else 'orin' if 'orin' in sys.argv else 'nano',
    nand='nand' in sys.argv,
    v05='v05' in sys.argv,
    device=device,
)

if 'enable' in sys.argv:
    with esp.pin_config():
        print('Enabling ESP...')
        esp.enable()
    sys.exit()

if 'reset' in sys.argv:
    with esp.pin_config():
        print('Resetting ESP...')
        esp.reset()
    sys.exit()

with esp.pin_config(), esp.flash_mode():
    if erase_flash:
        print('Erasing Flash...')
        erase_result = subprocess.run([
            'esptool.py',
            '--chip', 'esp32',
            '--port', esp.device,
            '--baud', '921600',
            '--before', 'default_reset',
            '--after', 'hard_reset',
            'erase_flash',
        ], check=False)
        if erase_result.returncode != 0:
            print('Failed to erase flash.')
            sys.exit(1)  # Exit if the erase fails

    print('Flashing...')
    result = subprocess.run([
        'esptool.py',
        '--chip', 'esp32',
        '--port', esp.device,
        '--baud', '921600',
        '--before', 'default_reset',
        '--after', 'hard_reset',
        'write_flash',
        '-z',
        '--flash_mode', 'dio',
        '--flash_freq', '40m',
        '--flash_size', 'detect',
        '0x1000', 'build/bootloader/bootloader.bin',
        '0x8000', 'build/partition_table/partition-table.bin',
        '0x20000', 'build/lizard.bin',
    ], check=False)
    if result.returncode != 0:
        print('Flashing failed. Maybe you need different parameters? Or you forgot "sudo"?\n')
        show_help()
