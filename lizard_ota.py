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
    parser.add_argument('-d', '--debug', action='store_true',
                        help='Debug output (shows ready signals and protocol details)')
    parser.add_argument('--target', default='core',
                        help='OTA target (e.g. core, p0, plexus, etc.; default: core)')

    return parser.parse_args()


def log(message):
    """Log message with OTA prefix."""
    print(f"[OTA] {message}", flush=True)


def connect_serial(port, baudrate):
    """Connect to serial port."""
    try:
        device = serial.Serial(port=port, baudrate=baudrate, timeout=SERIAL_TIMEOUT)
        log(f"Connected to {port} at {baudrate} baud")

        # Flush any old messages from the buffer
        device.reset_input_buffer()
        device.reset_output_buffer()
        time.sleep(0.1)  # Give time for buffers to clear

        return device
    except Exception as e:
        print(f"Error: Failed to connect to {port}: {e}")
        return None


# Send OTA command string
def send_ota_command(device, command, debug=False):
    """Send core.ota() command and wait for response."""
    try:
        log(f"Sending {command.strip()} command...")
        device.write(command.encode())

        start_time = time.time()
        while time.time() - start_time < OTA_COMMAND_TIMEOUT:
            if device.in_waiting > 0:
                response = device.readline().decode().strip()
                if debug:
                    log(f"Device: {response}")

                if "Starting UART OTA" in response:
                    log("OTA started successfully!")
                    return True
                elif "Starting UART bridge" in response:
                    log("Bridge started successfully!")
                    return True
            time.sleep(0.1)

        print("Error: No response to OTA command")
        return False

    except Exception as e:
        print(f"Error sending OTA command: {e}")
        return False


def wait_for_ready(device, timeout, debug=False):
    """Wait for device to be ready for firmware transfer."""
    log("Waiting for device to be ready...")
    start_time = time.time()

    while time.time() - start_time < timeout:
        if device.in_waiting > 0:
            line = device.readline().decode().strip()
            if debug:
                log(f"Device: {line}")

            if "Ready for firmware download" in line:
                log("Device ready for firmware!")
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
                        return True
                    else:
                        # Only show non-index data in debug mode
                        if debug:
                            print(f'DEBUG: Non-index data: {line}')
                except UnicodeDecodeError:
                    if debug:
                        print(f'DEBUG: Binary data received: {line}')

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

            # Wait for final confirmation from device
            if not wait_for_ready_signal(device, timeout=3, debug=debug):
                print(f'\nDevice not ready for final confirmation after {bytes_sent:,} bytes')
                return False

            print(f'\n✅ Transfer complete: {bytes_sent:,} bytes sent in {chunk_count} chunks')

            print('Listening for device OTA confirmation...')
            start_time = time.time()
            response_status = 'unknown'  # 'success', 'failure', or 'unknown'
            status_message = None

            while time.time() - start_time < DEVICE_RESPONSE_TIMEOUT+3:
                if device.in_waiting > 0:
                    try:
                        line = device.readline().decode(errors='replace').strip()
                        if line and not line.isspace():
                            # Always show in debug mode
                            if debug:
                                print(f'DEBUG: {line}')

                            # Check for success conditions
                            lcline = line.lower()
                            if 'ota ok' in lcline or 'restarting' in lcline:
                                response_status = 'success'
                                status_message = line
                                print('✅ Device confirmed OTA success!')
                                return True

                            # Check for failure conditions
                            if 'uart ota failed' in lcline:
                                response_status = 'failure'
                                status_message = line
                                print(f'❌ OTA Failed: {line}')
                                return False

                    except UnicodeDecodeError:
                        continue

                time.sleep(0.1)

            # Handle timeout cases
            if response_status == 'success':
                print('✅ Transfer completed successfully!')
                return True
            elif response_status == 'failure':
                print(f'❌ OTA Failed: {status_message}')
                return False
            else:
                print('⚠️  Transfer completed - status unknown (no clear success/failure detected)')
                return True

    except Exception as e:
        print(f'Error during transfer: {e}')
        return False


def perform_ota(port: str, baudrate: int, firmware_path: str, target: str, debug: bool = False) -> bool:
    '''Perform complete OTA update.'''
    print('🦎 Starting Lizard UART OTA...')

    device = connect_serial(port, baudrate)
    if not device:
        return False

    try:
        ota_cmd = f'{target}.ota()\n'
        if not send_ota_command(device, ota_cmd, debug):
            return False

        if not wait_for_ready(device, DEFAULT_TIMEOUT, debug):
            return False

        if not send_firmware(device, firmware_path, debug):
            return False

        return True

    finally:
        device.close()
        log('Disconnected')


def main() -> None:
    args = parse_args()

    try:
        success = perform_ota(
            args.port,
            args.baudrate,
            args.firmware,
            args.target,
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
