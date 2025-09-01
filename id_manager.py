#!/usr/bin/env python3
import sys
import time
import argparse
from typing import Optional

import serial


def send(port, line_: str) -> None:
    checksum_ = 0
    for c in line_:
        checksum_ ^= ord(c)
    port.write((f'{line_}@{checksum_:02x}\n').encode())


def wait_for_response(port, expected_prefix: str, timeout: float = 3.0) -> str:
    """Wait for a response line that starts with the expected prefix."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            line = port.read_until(b'\r\n').decode().rstrip()
        except UnicodeDecodeError:
            continue
        if line.startswith(expected_prefix):
            return line
    raise TimeoutError(f'Timeout waiting for response starting with "{expected_prefix}"')


def add_target_prefix(command: str, target: Optional[str]) -> str:
    """Add target prefix to command if target is specified."""
    if target is not None:
        return f'${target}{command}'
    return command


def get_device_id(port, target: Optional[str] = None) -> str:
    """Get current device ID from the device."""
    command = 'core.get_device_id()'
    prefixed_command = add_target_prefix(command, target)

    print(f"Getting current device ID{f' from target {target}' if target else ''}...")
    send(port, prefixed_command)

    try:
        response = wait_for_response(port, 'Device ID:')

        # Extract the ID from response (format: "Device ID: X@checksum")
        if 'Device ID:' in response:
            current_id_raw = response.split('Device ID:')[1].strip()
            # Strip off checksum if present (everything after @)
            current_id = current_id_raw.split('@')[0]
            return current_id
        else:
            print(f'✗ ERROR: Unexpected response format: {response}')
            sys.exit(1)

    except TimeoutError:
        print('ERROR: Timeout waiting for get response')
        sys.exit(1)


def set_device_id(port, device_id: int, target: Optional[str] = None) -> None:
    """Set device ID on the device and verify it was set correctly."""
    if device_id < 0 or device_id > 9:
        print('ERROR: Device ID must be between 0 and 9')
        sys.exit(1)

    print(f"Setting device ID to {device_id}{f' on target {target}' if target else ''}...")

    # Set the device ID
    set_command = f'core.set_device_id({device_id})'
    prefixed_set_command = add_target_prefix(set_command, target)
    send(port, prefixed_set_command)

    # Wait for confirmation that it was set
    try:
        wait_for_response(port, 'Device ID set to')
    except TimeoutError:
        print(f'ERROR: Timeout waiting for set confirmation for ID {device_id}')
        sys.exit(1)

    # Small delay before checking
    time.sleep(0.1)

    # Get the device ID to verify
    get_command = 'core.get_device_id()'
    prefixed_get_command = add_target_prefix(get_command, target)
    send(port, prefixed_get_command)

    # Wait for the device ID response
    try:
        response = wait_for_response(port, 'Device ID:')

        # Extract the ID from response (format: "Device ID: X@checksum")
        if 'Device ID:' in response:
            returned_id_raw = response.split('Device ID:')[1].strip()
            # Strip off checksum if present (everything after @)
            returned_id = returned_id_raw.split('@')[0]
            expected_id = str(device_id)

            if returned_id == expected_id:
                print(f'✓ SUCCESS: Device ID {device_id} set and verified correctly')
            else:
                print(f'✗ ERROR: Expected ID {expected_id}, got {returned_id}')
                sys.exit(1)
        else:
            print(f'✗ ERROR: Unexpected response format: {response}')
            sys.exit(1)

    except TimeoutError:
        print(f'ERROR: Timeout waiting for get response for ID {device_id}')
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description='Device ID management for Lizard devices')
    parser.add_argument('-p', '--port', default='/dev/ttyUSB0',
                        help='Serial device path (default: /dev/ttyUSB0)')
    parser.add_argument('--target', '-tar',
                        help='Target device name for command prefixing (e.g., p0, plexus)')

    # Create mutually exclusive group for get/set operations
    operation_group = parser.add_mutually_exclusive_group(required=True)
    operation_group.add_argument('--get', action='store_true',
                                 help='Get current device ID')
    operation_group.add_argument('--set', type=int, metavar='ID',
                                 help='Set device ID (0-9)')

    args = parser.parse_args()

    usb_path = args.port

    with serial.Serial(usb_path, baudrate=115200, timeout=1.0) as port:
        if args.get:
            # Get the current device ID
            current_id = get_device_id(port, args.target)
            target_info = f' (target: {args.target})' if args.target else ''
            print(f'Current device ID{target_info}: {current_id}')
        elif args.set is not None:
            # Set and verify the device ID
            set_device_id(port, args.set, args.target)


if __name__ == '__main__':
    main()
