#!/usr/bin/env python3
import sys
import time
from pathlib import Path

import serial

def show_help() -> None:
    print(f'Usage: {sys.argv[0]} <id> <device_path> [after_flash]')
    print('   <id>           ID between 0 and 99')
    print('   <device_path>  Path to the serial device')
    print('   after_flash    Optional flag to indicate this is being run after flashing')
    sys.exit(1)

if len(sys.argv) < 3 or any(h in sys.argv for h in ['--help', '-help', 'help', '-h']):
    show_help()

# Parse arguments
try:
    device_id = int(sys.argv[1])
    if device_id < 0 or device_id > 99:
        print("Error: ID must be between 0 and 99")
        sys.exit(1)
except ValueError:
    print("Error: ID must be an integer")
    sys.exit(1)

usb_path = sys.argv[2]
after_flash = len(sys.argv) > 3 and sys.argv[3] == "after_flash"

def send(line_: str) -> None:
    print(f'Sending: {line_}')
    checksum_ = 0
    for c in line_:
        checksum_ ^= ord(c)
    port.write((f'{line_}@{checksum_:02x}\n').encode())

with serial.Serial(usb_path, baudrate=115200, timeout=1.0) as port:
    # If after_flash is True, wait for "Ready." message
    if after_flash:
        print("Waiting for ESP32 to boot...")
        timeout = 10.0  # 10 seconds timeout
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = port.read_until(b'\r\n').decode().rstrip()
                print(f"Received: {line}")
                if line == 'Ready.':
                    print('ESP32 booted and sent "Ready."')
                    break
            except UnicodeDecodeError:
                continue
        else:
            raise TimeoutError('Timeout waiting for device to restart!')
    
    # Set the device ID
    send(f'core.set_device_id({device_id})')
    
    # Wait for confirmation
    timeout = 10.0
    deadline = time.time() + timeout
    expected_response = f"Device ID set to {device_id//10}{device_id%10}"
    while time.time() < deadline:
        try:
            line = port.read_until(b'\r\n').decode().rstrip()
            if expected_response in line:
                print(f"Received: {line}")
                print(f'Successfully set device ID to {device_id}')
                break
        except UnicodeDecodeError:
            continue
    else:
        raise TimeoutError('Timeout waiting for confirmation!') 