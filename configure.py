#!/usr/bin/env python3
import argparse
import json
import time
from pathlib import Path
from typing import Iterator

import serial

parser = argparse.ArgumentParser(description='Configure an ESP32 running Lizard firmware')
parser.add_argument('config_file', help='Path to the .liz configuration file')
parser.add_argument('device_path', help='Serial device path (e.g., /dev/ttyUSB0)')
parser.add_argument('--serial-bus', type=int, metavar='NODE_ID',
                    help='Send configuration via serial bus to the specified node ID')
parser.add_argument('--bus-name', default='bus',
                    help='Name of the SerialBus module (default: bus)')
args = parser.parse_args()


def send(payload: str) -> None:
    """Send a payload string to the ESP32, optionally over a serial bus."""
    line_ = f'{args.bus_name}.send({args.serial_bus}, {json.dumps(payload)})' if args.serial_bus else payload
    print(f'Sending: {line_}')
    checksum_ = 0
    for c in line_:
        checksum_ ^= ord(c)
    port.write((f'{line_}@{checksum_:02x}\n').encode())


def read(timeout: float) -> Iterator[str]:
    """Yield lines read from the serial port until the timeout expires."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            yield port.read_until(b'\r\n').decode().rstrip()
        except UnicodeDecodeError:
            continue


with serial.Serial(args.device_path, baudrate=115200, timeout=1.0) as port:
    startup = Path(args.config_file).read_text('utf-8') + '\n'
    checksum = sum(ord(c) for c in startup) % 0x10000

    send('!-')
    for line in startup.splitlines():
        send(f'!+{line}')
    send('!.')
    send('core.restart()')

    reboot_timeout = 3.0 + 3.0 * startup.count('Expander')

    if args.serial_bus:
        target = f'node {args.serial_bus}'
        prefix = f'{args.bus_name}[{args.serial_bus}]: checksum: '
        print(f'Waiting {reboot_timeout:.1f}s for {target} to reboot...')
        time.sleep(reboot_timeout)
    else:
        target = 'ESP32'
        prefix = 'checksum: '
        for line in read(reboot_timeout):
            if line == 'Ready.':
                print('ESP32 booted and sent "Ready."')
                break
        else:
            raise TimeoutError('Timeout waiting for device to restart!')

    send('core.startup_checksum()')
    for line in read(5.0):
        if len(line) > 3 and line[-3] == '@':
            line = line[:-3]
        if prefix in line:
            received = int(line[line.index(prefix) + len(prefix):], 16)
            if received == checksum:
                print(f'{target} checksum matches.')
                break
            raise ValueError(f'{target} checksum mismatch! expected {checksum:#06x}, got {received:#06x}')
    else:
        raise TimeoutError(f'Timeout waiting for {target} checksum!')
