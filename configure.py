#!/usr/bin/env python3
import argparse
import time
from pathlib import Path

import serial

parser = argparse.ArgumentParser(description='Configure an ESP32 running Lizard firmware')
parser.add_argument('config_file', help='Path to the .liz configuration file')
parser.add_argument('device_path', help='Serial device path (e.g., /dev/ttyUSB0)')
parser.add_argument('--serial-bus', type=int, metavar='NODE_ID',
                    help='Send configuration via serial bus to the specified node ID')
args = parser.parse_args()


def send(line_: str) -> None:
    print(f'Sending: {line_}')
    checksum_ = 0
    for c in line_:
        checksum_ ^= ord(c)
    port.write((f'{line_}@{checksum_:02x}\n').encode())


def configure(payload: str) -> None:
    if args.serial_bus:
        send(f"bus.send({args.serial_bus}, '{payload}')")
    else:
        send(payload)


def read_lines(timeout: float):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            yield port.read_until(b'\r\n').decode().rstrip()
        except UnicodeDecodeError:
            continue


with serial.Serial(args.device_path, baudrate=115200, timeout=1.0) as port:
    startup = Path(args.config_file).read_text('utf-8')
    if not startup.endswith('\n'):
        startup += '\n'
    checksum = sum(ord(c) for c in startup) % 0x10000

    configure('!-')
    for line in startup.splitlines():
        configure(f'!+{line}')
    configure('!.')
    configure('core.restart()')

    reboot_timeout = 3.0 + 3.0 * startup.count('Expander')

    if args.serial_bus:
        target = f'node {args.serial_bus}'
        prefix = f'bus[{args.serial_bus}]: checksum: '
        print(f'Waiting {reboot_timeout:.1f}s for {target} to reboot...')
        time.sleep(reboot_timeout)

        configure('core.startup_checksum()')
        for line in read_lines(5.0):
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
    else:
        for line in read_lines(reboot_timeout):
            if line == 'Ready.':
                print('ESP32 booted and sent "Ready."')
                break
        else:
            raise TimeoutError('Timeout waiting for device to restart!')

        send('core.startup_checksum()')
        for line in read_lines(3.0):
            if line.startswith('checksum: '):
                received = int(line.split()[1].split('@')[0], 16)
                if received == checksum:
                    print('Checksum matches.')
                    break
                raise ValueError('Checksum mismatch!')
        else:
            raise TimeoutError('Timeout waiting for checksum!')
