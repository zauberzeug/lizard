#!/usr/bin/env python3
import sys
import time
from pathlib import Path

import serial

if len(sys.argv) != 3:
    print(f'Usage: {sys.argv[0]} <config_file> <device_path>')
    exit()

txt_path, usb_path = sys.argv[1:]


def send(line) -> None:
    print(f'Sending: {line}')
    checksum = 0
    for c in line:
        checksum ^= ord(c)
    port.write((f'{line}@{checksum:02x}\n').encode())


with serial.Serial(usb_path, baudrate=115200, timeout=1.0) as port:
    startup = Path(txt_path).read_text()
    if not startup.endswith('\n'):
        startup += '\n'
    checksum = sum(ord(c) for c in startup) % 0x10000

    send('!-')
    for line in startup.splitlines():
        send(f'!+{line}')
        time.sleep(0.1)
    send('!.')
    send('core.restart()')

    time.sleep(3.0)
    send('core.startup_checksum()')
    deadline = time.time() + 1.0
    while time.time() < deadline:
        try:
            line = port.read_until(b'\r\n').decode().rstrip()
        except UnicodeDecodeError:
            continue
        if line.startswith('checksum: '):
            if int(line.split()[1].split("@")[0], 16) == checksum:
                print('Checksum matches.')
                break
            else:
                raise ValueError('Checksum mismatch!')
    else:
        raise TimeoutError('Timeout waiting for checksum!')
