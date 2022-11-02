#!/usr/bin/env python3
import subprocess
import sys

from esp import Esp


def help() -> None:
    print(f'{sys.argv[0]} [nano | xavier] [nand]')
    print(f'   nano          flashing Jetson Nano (default)')
    print(f'   xavier        flashing Jetson Xavier')
    print(f'   nand          Robot Brain has piggyboard with NAND gate (eg. older version)')
    print(f'   usb           use /dev/tty.SLAB_USBtoUART as serial device')
    print(f'   /dev/<name>   use /dev/<name> as serial device')


if any(h in sys.argv for h in ['--help', '-help', 'help']):
    help()
    sys.exit()

device = None
if 'usb' in sys.argv:
    device = '/dev/tty.SLAB_USBtoUART'
for p in sys.argv:
    if p.startswith('/dev/'):
        device = p
esp = Esp(nand='nand' in sys.argv, xavier='xavier' in sys.argv, device=device)

with esp.pin_config(), esp.flash_mode():
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
        '0x10000', 'build/lizard.bin',
    ])
    if result.returncode != 0:
        print('Flashing failed. Maybe you need different parameters? Or you forgot "sudo"?\n')
        help()
