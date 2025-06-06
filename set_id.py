#!/usr/bin/env python3
import sys
import time
import argparse

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


def main():
    parser = argparse.ArgumentParser(description='Set or get device ID on serial device')
    parser.add_argument('device_id', type=int, nargs='?',
                        help='Device ID to set (0-9). If not provided, will get current ID')
    parser.add_argument('-p', '--port', default='/dev/ttyUSB0', help='Serial device path (default: /dev/ttyUSB0)')

    args = parser.parse_args()

    usb_path = args.port

    with serial.Serial(usb_path, baudrate=115200, timeout=1.0) as port:
        if args.device_id is None:
            # Just get the current device ID
            print(f"Getting current device ID from {usb_path}...")
            send(port, 'core.get_device_id()')

            try:
                response = wait_for_response(port, "Device ID:")

                # Extract the ID from response (format: "Device ID: X@checksum")
                if "Device ID:" in response:
                    current_id_raw = response.split("Device ID:")[1].strip()
                    # Strip off checksum if present (everything after @)
                    current_id = current_id_raw.split("@")[0]
                    print(f'Current device ID: {current_id}')
                else:
                    print(f'✗ ERROR: Unexpected response format: {response}')
                    sys.exit(1)

            except TimeoutError:
                print('ERROR: Timeout waiting for get response')
                sys.exit(1)
        else:
            # Set and verify the device ID
            device_id = args.device_id

            if device_id < 0 or device_id > 9:
                print('ERROR: Device ID must be between 0 and 9')
                sys.exit(1)

            print(f"Setting device ID to {device_id} on {usb_path}...")

            # Set the device ID
            send(port, f'core.set_device_id({device_id})')

            # Wait for confirmation that it was set
            try:
                wait_for_response(port, "Device ID set to")
            except TimeoutError:
                print(f'ERROR: Timeout waiting for set confirmation for ID {device_id}')
                sys.exit(1)

            # Small delay before checking
            time.sleep(0.1)

            # Get the device ID to verify
            send(port, 'core.get_device_id()')

            # Wait for the device ID response
            try:
                response = wait_for_response(port, "Device ID:")

                # Extract the ID from response (format: "Device ID: X@checksum")
                if "Device ID:" in response:
                    returned_id_raw = response.split("Device ID:")[1].strip()
                    # Strip off checksum if present (everything after @)
                    returned_id = returned_id_raw.split("@")[0]
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


if __name__ == '__main__':
    main()
