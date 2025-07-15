#!/usr/bin/env python3
import argparse
import serial
import sys
import os
import time

# Constants
DEFAULT_PORT = '/dev/tty.SLAB_USBtoUART'
DEFAULT_BAUDRATE = 115200
DEFAULT_TIMEOUT = 30
SERIAL_TIMEOUT = 0.1
CHUNK_SIZE = 512
PROGRESS_REPORT_INTERVAL = 5  # Report every 5%
READY_SIGNAL_TIMEOUT = 2
OTA_COMMAND_TIMEOUT = 10
DEVICE_RESPONSE_TIMEOUT = 10


def parse_args():
    parser = argparse.ArgumentParser(description='Perform UART OTA update for Lizard firmware')

    parser.add_argument('firmware', help='Path to firmware binary file')
    parser.add_argument('-p', '--port', default=DEFAULT_PORT,
                        help=f'Serial port (default: {DEFAULT_PORT})')
    parser.add_argument('-b', '--baudrate', type=int, default=DEFAULT_BAUDRATE,
                        help=f'Baud rate (default: {DEFAULT_BAUDRATE})')
    parser.add_argument('-t', '--timeout', type=int, default=DEFAULT_TIMEOUT,
                        help=f'OTA timeout in seconds (default: {DEFAULT_TIMEOUT})')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Verbose output')
    parser.add_argument('--target', choices=['core', 'p0'], default='core',
                        help='OTA target (core or p0, default: core)')

    return parser.parse_args()


def log(message, verbose=False, force=False):
    """Log message if verbose or force is True."""
    if verbose or force:
        print(f"[OTA] {message}")


def connect_serial(port, baudrate, verbose=False):
    """Connect to serial port."""
    try:
        device = serial.Serial(port=port, baudrate=baudrate, timeout=SERIAL_TIMEOUT)
        log(f"Connected to {port} at {baudrate} baud", verbose, force=True)
        return device
    except Exception as e:
        print(f"Error: Failed to connect to {port}: {e}")
        return None


# Send OTA command string
def send_ota_command(device, command, verbose=False):
    """Send core.ota() command and wait for response."""
    try:
        log(f"Sending {command.strip()} command...", verbose, force=True)
        device.write(command.encode())

        start_time = time.time()
        while time.time() - start_time < OTA_COMMAND_TIMEOUT:
            if device.in_waiting > 0:
                response = device.readline().decode().strip()
                log(f"Device: {response}", verbose)

                if "Starting UART OTA" in response:
                    log("OTA started successfully!", verbose, force=True)
                    return True
            time.sleep(0.1)

        print("Error: No response to OTA command")
        return False

    except Exception as e:
        print(f"Error sending OTA command: {e}")
        return False


def wait_for_ready(device, timeout, verbose=False):
    """Wait for device to be ready for firmware transfer."""
    log("Waiting for device to be ready...", verbose, force=True)
    start_time = time.time()

    while time.time() - start_time < timeout:
        if device.in_waiting > 0:
            line = device.readline().decode().strip()
            log(f"Device: {line}", verbose, force=True)

            if "Ready for firmware download" in line:
                log("Device ready for firmware!", verbose, force=True)
                return True
            elif "failed" in line.lower() or "error" in line.lower():
                print(f"Error: Device reported: {line}")
                return False

        time.sleep(0.1)

    print(f"Error: Timeout waiting for device ready ({timeout}s)")
    return False


def wait_for_ready_signal(device, timeout=READY_SIGNAL_TIMEOUT):
    """Wait for \\r\\n ready signal from device."""
    start_time = time.time()

    while time.time() - start_time < timeout:
        if device.in_waiting > 0:
            data = device.read(1)
            if data == b'\r':
                # Check if next byte is \n (or if there's more data available)
                if device.in_waiting > 0:
                    next_byte = device.read(1)
                    if next_byte == b'\n':
                        print(" \r\n ready signal received")
                        return True
                else:
                    # Try to read next byte with a small timeout
                    next_byte = device.read(1)
                    if next_byte == b'\n':
                        print(" \r\n ready signal received 2")
                        return True
        time.sleep(0.01)

    return False


def send_firmware(device, firmware_path, verbose=False):
    """Send firmware binary using ultra-simple protocol."""
    if not os.path.exists(firmware_path):
        print(f"Error: Firmware file not found: {firmware_path}")
        return False

    file_size = os.path.getsize(firmware_path)
    print(f"Sending firmware: {firmware_path} ({file_size:,} bytes)")

    try:
        print("✅ Device is ready! Starting data transfer...")
        print("Starting firmware transfer...")

        with open(firmware_path, 'rb') as firmware_file:
            bytes_sent = 0
            last_progress = -1

            # Wait for initial ready signal
            if not wait_for_ready_signal(device, timeout=10):
                print("Device not ready for initial transfer")
                return False

            while bytes_sent < file_size:
                chunk_data = firmware_file.read(CHUNK_SIZE)
                if not chunk_data:
                    break

                device.write(chunk_data)
                device.flush()
                bytes_sent += len(chunk_data)

                # Progress reporting
                progress = int((bytes_sent / file_size) * 100)
                if progress % PROGRESS_REPORT_INTERVAL == 0 and progress != last_progress:
                    print(f"Progress: {progress}% ({bytes_sent:,}/{file_size:,} bytes)")
                    last_progress = progress

                # Wait for ready signal before sending next chunk
                if bytes_sent < file_size:
                    if not wait_for_ready_signal(device, timeout=3):
                        print(f"Device not ready for more data at {bytes_sent:,} bytes")
                        return False

        print(f"Transfer complete: {bytes_sent:,} bytes sent")

        # Listen for device response
        print("Listening for device response...")
        start_time = time.time()
        while time.time() - start_time < DEVICE_RESPONSE_TIMEOUT:
            if device.in_waiting > 0:
                try:
                    line = device.readline().decode().strip()
                    if line and not line.isspace():
                        print(f"Device: {line}")

                        if "OTA OK" in line or "restarting" in line.lower():
                            print("✅ Device confirmed OTA success!")
                            return True
                        elif "failed" in line.lower() or "error" in line.lower():
                            print("❌ Device reported OTA failure!")
                            return False
                except UnicodeDecodeError:
                    continue  # Skip garbled data

            time.sleep(0.1)

        print("✅ Transfer completed - assuming success (no errors detected)")
        return True

    except Exception as e:
        print(f"Error during transfer: {e}")
        return False


# target parameter indicates which device (core or p0)
def perform_ota(port, baudrate, firmware_path, timeout, target, verbose=False):
    """Perform complete OTA update."""
    print("🦎 Starting Lizard UART OTA...")

    device = connect_serial(port, baudrate, verbose)
    if not device:
        return False

    try:
        # Determine which OTA command to send based on target
        ota_cmd = "core.ota_p0()\n" if target == "p0" else "core.ota()\n"

        # Send OTA command
        if not send_ota_command(device, ota_cmd, verbose):
            return False

        # Wait for device ready
        if not wait_for_ready(device, timeout, verbose):
            return False

        # Send firmware
        if not send_firmware(device, firmware_path, verbose):
            return False

        print("✅ OTA completed successfully!")
        print("Device should reboot with new firmware.")
        return True

    finally:
        device.close()
        log("Disconnected", verbose)


def main():
    args = parse_args()

    try:
        success = perform_ota(
            args.port,
            args.baudrate,
            args.firmware,
            args.timeout,
            args.target,
            args.verbose
        )
        sys.exit(0 if success else 1)

    except KeyboardInterrupt:
        print("\\n❌ OTA interrupted")
        sys.exit(1)
    except Exception as e:
        print(f"❌ OTA failed: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
