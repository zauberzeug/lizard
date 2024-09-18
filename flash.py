#!/usr/bin/env python3
import subprocess
import sys

from esp import Esp


def help() -> None:
    print(f'{sys.argv[0]} [nano | xavier | orin] [nand | v05] [usb | /dev/<name>] [enable] [-e | --erase]')
    print(f'   -e, --erase   erase the flash before flashing the new firmware')
    print(f'   nano          flashing Jetson Nano (default)')
    print(f'   xavier        flashing Jetson Xavier')
    print(f'   orin          flashing Jetson Orin')
    print(f'   nand          Robot Brain has piggyboard with NAND gate (eg. older version)')
    print(f'   v05           Robot Brain has piggyboard with V0.5 or higher (eg. newer version)')
    print(f'   usb           use /dev/tty.SLAB_USBtoUART as serial device')
    print(f'   /dev/<name>   use /dev/<name> as serial device')
    print(f'   enable        enable the ESP32 microcontroller')

if any(h in sys.argv for h in ['--help', '-help', 'help', '-h']):
    help()
    sys.exit()

erase_flash = any(e in sys.argv for e in ['-e', '--erase'])
device = None
if 'usb' in sys.argv:
    device = '/dev/tty.SLAB_USBtoUART'
for p in sys.argv:
    if p.startswith('/dev/'):
        device = p

esp = Esp(nand='nand' in sys.argv, xavier='xavier' in sys.argv, orin='orin' in sys.argv, v05='v05' in sys.argv, device=device)

def check_flash_size(device: str) -> bool:
    """Checks if the ESP32 has 8MB flash using esptool.py."""
    try:
        # Run esptool.py to get chip info, including flash size
        print(f"Checking flash size on {device}...")
        result = subprocess.run(
            ['esptool.py', '--chip', 'esp32', '--port', device, 'flash_id'],
            capture_output=True, text=True, check=True
        )

        # Search the output for the flash size information
        output = result.stdout
        if '8MB' in output or '8388608' in output:
            print("Device has 8MB flash.")
            return True
        else:
            print("Device does not have 8MB flash.")
            return False
    except subprocess.CalledProcessError as e:
        print(f"Failed to check flash size: {e}")
        return False

# Check if the device should be enabled
if 'enable' in sys.argv:
    with esp.pin_config():
        print('Enabling ESP...')
        esp.activate()
    sys.exit()

# Check flash size before proceeding
if not check_flash_size(esp.device):
    print("Aborting: Device does not have 8MB flash.")
    sys.exit(1)

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
        help()
