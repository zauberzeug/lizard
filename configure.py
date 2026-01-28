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
    if args.serial_bus is not None:
        send(f'bus.send({args.serial_bus}, "{payload}")')
    else:
        send(payload)


with serial.Serial(args.device_path, baudrate=115200, timeout=1.0) as port:
    startup = Path(args.config_file).read_text('utf-8')
    if not startup.endswith('\n'):
        startup += '\n'
    checksum = sum(ord(c) for c in startup) % 0x10000

    configure('!-')
    for line in startup.splitlines():
        if line.strip():
            configure(f'!+{line}')
    configure('!.')
    configure('core.restart()')

    if args.serial_bus is None:
        # Direct connection: wait for "Ready." then check checksum
        timeout = 3.0 + 3.0 * startup.count('Expander')
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = port.read_until(b'\r\n').decode().rstrip()
            except UnicodeDecodeError:
                continue
            if line == 'Ready.':
                print('ESP32 booted and sent "Ready."')
                break
        else:
            raise TimeoutError('Timeout waiting for device to restart!')

        # Immediately check checksum after ready
        send('core.startup_checksum()')
        deadline = time.time() + 3.0
        while time.time() < deadline:
            try:
                line = port.read_until(b'\r\n').decode().rstrip()
            except UnicodeDecodeError:
                continue
            if line.startswith('checksum: '):
                received = line.split()[1].split('@')[0]
                if int(received, 16) == checksum:
                    print('Checksum matches.')
                    break
                else:
                    raise ValueError('Checksum mismatch!')
        else:
            raise TimeoutError('Timeout waiting for checksum!')
    else:
        # Serial bus: wait for reboot, then poll for checksum
        checksum_prefix = f'bus[{args.serial_bus}]: checksum: '
        target_name = f'node {args.serial_bus}'
        reboot_timeout = 3.0 + 3.0 * startup.count('Expander')
        print(f'Waiting {reboot_timeout:.1f}s for {target_name} to reboot...')
        time.sleep(reboot_timeout)

        # Poll for checksum response
        configure('core.startup_checksum()')
        deadline = time.time() + 5.0
        while time.time() < deadline:
            try:
                line = port.read_until(b'\r\n').decode().rstrip()
            except UnicodeDecodeError:
                continue
            # Strip checksum suffix if present (format: @XX at end)
            if len(line) > 3 and line[-3] == '@':
                line = line[:-3]
            if line.startswith(checksum_prefix):
                received_checksum = int(line[len(checksum_prefix):], 16)
                if received_checksum == checksum:
                    print(f'{target_name} checksum matches.')
                    break
                else:
                    raise ValueError(f'{target_name} checksum mismatch!')
        else:
            raise TimeoutError(f'Timeout waiting for {target_name} checksum!')
