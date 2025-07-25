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
    parser.add_argument('-d', '--debug', action='store_true',
                        help='Debug output (shows ready signals and protocol details)')
    parser.add_argument('--target', default='core',
                        help='OTA target (e.g. core, p0, plexus, etc.; default: core)')
    parser.add_argument('--bridge', default='',
                        help='Comma-separated list of bridge modules to activate before OTA (e.g. p0,p1)')

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
                elif "Starting UART bridge" in response:
                    log("Bridge started successfully!", verbose, force=True)
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


def wait_for_ready_signal(device, timeout=READY_SIGNAL_TIMEOUT, debug=False):
    '''Wait for index\\r\\n ready signal from device.'''
    start_time = time.time()
    buffer = b''

    while time.time() - start_time < timeout:
        if device.in_waiting > 0:
            data = device.read(device.in_waiting)
            buffer += data

            # Look for complete messages ending with \r\n
            while b'\r\n' in buffer:
                line_end = buffer.find(b'\r\n')
                line = buffer[:line_end]
                buffer = buffer[line_end + 2:]  # Remove processed line including \r\n

                try:
                    # Try to decode as index number
                    index_str = line.decode('utf-8').strip()
                    if index_str.isdigit():
                        if debug:
                            print(f'Ready signal received: {index_str}')
                        return True
                    else:
                        # Only show non-index data in debug mode
                        if debug:
                            print(f'Non-index data: {line}')
                except UnicodeDecodeError:
                    if debug:
                        print(f'Binary data received: {line}')

        time.sleep(0.01)

    return False


def show_progress_bar(current: int, total: int, chunk_count: int) -> None:
    '''Display animated progress bar with statistics.'''
    percent = int(current * 100 / total)
    bar_width = 40
    filled = int(bar_width * percent / 100)
    bar = '█' * filled + '░' * (bar_width - filled)

    # Calculate transfer rate
    mb_current = current / 1024 / 1024
    mb_total = total / 1024 / 1024

    print(f'\r[{bar}] {percent:3d}% | {mb_current:.1f}/{mb_total:.1f} MB | {chunk_count} chunks',
          end='', flush=True)


def send_firmware(device, firmware_path: str, debug: bool = False) -> bool:
    '''Send firmware binary using checksum-validated chunks.'''
    if not os.path.exists(firmware_path):
        print(f'Error: Firmware file not found: {firmware_path}')
        return False

    file_size = os.path.getsize(firmware_path)
    print(f'Sending firmware: {firmware_path} ({file_size:,} bytes)')

    def calculate_checksum(data: bytes) -> int:
        '''Calculate XOR checksum for data, same as Lizard UART protocol.'''
        checksum = 0
        for byte in data:
            checksum ^= byte
        return checksum

    try:
        print('✅ Device is ready! Starting data transfer...')

        with open(firmware_path, 'rb') as firmware_file:
            bytes_sent = 0
            chunk_count = 0

            if not wait_for_ready_signal(device, timeout=10, debug=debug):
                print('Device not ready for initial transfer')
                return False

            while bytes_sent < file_size:
                chunk_data = firmware_file.read(CHUNK_SIZE)
                if not chunk_data:
                    break

                chunk_count += 1

                checksum = calculate_checksum(chunk_data)

                device.write(chunk_data)
                checksum_suffix = f'\r\n@{checksum:02x}\r\n'.encode('ascii')
                device.write(checksum_suffix)
                device.flush()

                bytes_sent += len(chunk_data)

                # Update progress bar
                show_progress_bar(bytes_sent, file_size, chunk_count)

                if bytes_sent < file_size:
                    if not wait_for_ready_signal(device, timeout=3, debug=debug):
                        print(f'\nDevice not ready for more data at {bytes_sent:,} bytes')
                        return False

        print(f'\n✅ Transfer complete: {bytes_sent:,} bytes sent in {chunk_count} chunks')

        print('Listening for device response...')
        start_time = time.time()
        while time.time() - start_time < DEVICE_RESPONSE_TIMEOUT:
            if device.in_waiting > 0:
                try:
                    line = device.readline().decode().strip()
                    if line and not line.isspace():
                        print(f'Device: {line}')

                        if 'OTA OK' in line or 'restarting' in line.lower():
                            print('✅ Device confirmed OTA success!')
                            return True
                        elif 'failed' in line.lower() or 'error' in line.lower():
                            print('❌ Device reported OTA failure!')
                            return False
                except UnicodeDecodeError:
                    continue

            time.sleep(0.1)

        print('✅ Transfer completed - assuming success (no errors detected)')
        return True

    except Exception as e:
        print(f'Error during transfer: {e}')
        return False


def perform_ota(port: str, baudrate: int, firmware_path: str, timeout: int, target: str, bridge: str, verbose: bool = False, debug: bool = False) -> bool:
    '''Perform complete OTA update.'''
    print('🦎 Starting Lizard UART OTA...')

    device = connect_serial(port, baudrate, verbose)
    if not device:
        return False

    try:
        if bridge:
            for bridge_name in bridge.split(','):
                bridge_name = bridge_name.strip()
                if bridge_name:
                    bridge_cmd = f'{bridge_name}.ota_bridge_start()\n'
                    log(f'Sending bridge command: {bridge_cmd.strip()}', verbose, force=True)
                    if not send_ota_command(device, bridge_cmd, verbose):
                        print(f'Error: Bridge {bridge_name} did not respond')
                        return False

        ota_cmd = f'{target}.ota()\n'
        log(f'Sending OTA command: {ota_cmd.strip()}', verbose, force=True)
        if not send_ota_command(device, ota_cmd, verbose):
            return False

        if not wait_for_ready(device, timeout, verbose):
            return False

        if not send_firmware(device, firmware_path, debug):
            return False

        print('✅ OTA completed successfully!')
        print('Device should reboot with new firmware.')
        return True

    finally:
        device.close()
        log('Disconnected', verbose)


def main() -> None:
    args = parse_args()

    try:
        success = perform_ota(
            args.port,
            args.baudrate,
            args.firmware,
            args.timeout,
            args.target,
            args.bridge,
            args.verbose,
            args.debug
        )
        sys.exit(0 if success else 1)

    except KeyboardInterrupt:
        print('\n❌ OTA interrupted')
        sys.exit(1)
    except Exception as e:
        print(f'❌ OTA failed: {e}')
        sys.exit(1)


if __name__ == "__main__":
    main()
