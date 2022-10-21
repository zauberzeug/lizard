#!/usr/bin/env python3

import subprocess
from esp import Esp

esp = Esp()
with esp.pin_config(), esp.flash_mode():
    print('Flashing...')
    subprocess.run([
        'esptool.py', 
        '--chip', 'esp32', 
        '--port', '/dev/ttyTHS1', 
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
        '0x10000', 'build/lizard.bin'
    ])