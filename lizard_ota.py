#!/usr/bin/env python3
import argparse
import serial
import sys
import os
import time
from typing import Optional
from dataclasses import dataclass


@dataclass
class OTAConfig:
    '''Configuration for OTA operations.'''
    port: str = '/dev/tty.SLAB_USBtoUART'
    baudrate: int = 115200
    timeout: int = 30
    serial_timeout: float = 0.1
    chunk_size: int = 512
    ready_signal_timeout: float = 2.0
    ota_command_timeout: float = 10.0
    device_response_timeout: float = 10.0
    progress_bar_width: int = 40


class OTAProtocol:
    '''Constants for OTA protocol communication.'''
    # Device responses
    OTA_START_RESPONSES = ['Starting UART OTA', 'Starting UART bridge']
    READY_MESSAGE = 'Ready for firmware download'
    SUCCESS_INDICATORS = ['ota ok', 'restarting']
    FAILURE_INDICATORS = ['uart ota failed']

    # Protocol format
    CHECKSUM_SUFFIX_FORMAT = '\r\n@{:02x}\r\n'
    COMMAND_FORMAT = '{}.ota()\n'


class OTALogger:
    '''Centralized logging for OTA operations.'''

    @staticmethod
    def info(message: str) -> None:
        '''Log informational message.'''
        print(f'[OTA] {message}', flush=True)

    @staticmethod
    def debug(message: str) -> None:
        '''Log debug message.'''
        print(f'[OTA DEBUG] {message}', flush=True)

    @staticmethod
    def error(message: str) -> None:
        '''Log error message.'''
        print(f'[OTA ERROR] {message}', flush=True)

    @staticmethod
    def success(message: str) -> None:
        '''Log success message.'''
        print(f'✅ {message}', flush=True)

    @staticmethod
    def failure(message: str) -> None:
        '''Log failure message.'''
        print(f'❌ {message}', flush=True)

    @staticmethod
    def warning(message: str) -> None:
        '''Log warning message.'''
        print(f'⚠️  {message}', flush=True)


class ProgressBar:
    '''Handles progress display during firmware transfer.'''

    def __init__(self, total_size: int, bar_width: int = 40):
        self.total_size = total_size
        self.bar_width = bar_width
        self.last_percent = -1

    def update(self, current: int, chunk_count: int) -> None:
        '''Update and display progress bar.'''
        percent = int(current * 100 / self.total_size)

        # Only update if percentage changed
        if percent != self.last_percent:
            self.last_percent = percent
            filled = int(self.bar_width * percent / 100)
            bar = '█' * filled + '░' * (self.bar_width - filled)

            # Calculate transfer statistics
            mb_current = current / 1024 / 1024
            mb_total = self.total_size / 1024 / 1024

            print(f'\r[{bar}] {percent:3d}% | {mb_current:.1f}/{mb_total:.1f} MB | {chunk_count} chunks',
                  end='', flush=True)


class LizardOTA:
    '''Main OTA handler for Lizard firmware updates.'''

    def __init__(self, config: OTAConfig, debug: bool = False):
        self.config = config
        self.debug = debug
        self.device: Optional[serial.Serial] = None
        self.logger = OTALogger()

    def connect(self) -> bool:
        '''Connect to the serial device.'''
        try:
            self.device = serial.Serial(
                port=self.config.port,
                baudrate=self.config.baudrate,
                timeout=self.config.serial_timeout
            )
            self.logger.info(f'Connected to {self.config.port} at {self.config.baudrate} baud')

            # Flush any old messages from the buffer
            self.device.reset_input_buffer()
            self.device.reset_output_buffer()
            time.sleep(0.1)  # Give time for buffers to clear

            return True

        except Exception as e:
            self.logger.error(f'Failed to connect to {self.config.port}: {e}')
            return False

    def disconnect(self) -> None:
        '''Disconnect from the serial device.'''
        if self.device:
            self.device.close()
            self.logger.info('Disconnected')

    def send_command(self, command: str) -> bool:
        '''Send OTA command and wait for response.'''
        if not self.device:
            self.logger.error('No device connected')
            return False

        try:
            self.logger.info(f'Sending {command.strip()} command...')
            self.device.write(command.encode())

            start_time = time.time()
            while time.time() - start_time < self.config.ota_command_timeout:
                if self.device.in_waiting > 0:
                    response = self.device.readline().decode().strip()
                    if self.debug:
                        self.logger.debug(f'Device: {response}')

                    if any(indicator in response for indicator in OTAProtocol.OTA_START_RESPONSES):
                        self.logger.success('OTA started successfully!')
                        return True

                time.sleep(0.1)

            self.logger.error('No response to OTA command')
            return False

        except Exception as e:
            self.logger.error(f'Error sending OTA command: {e}')
            return False

    def wait_for_ready(self, timeout: Optional[float] = None) -> bool:
        '''Wait for device to be ready for firmware transfer.'''
        if not self.device:
            return False

        timeout = timeout or self.config.timeout
        self.logger.info('Waiting for device to be ready...')
        start_time = time.time()

        while time.time() - start_time < timeout:
            if self.device.in_waiting > 0:
                line = self.device.readline().decode().strip()
                if self.debug:
                    self.logger.debug(f'Device: {line}')

                if OTAProtocol.READY_MESSAGE in line:
                    self.logger.success('Device ready for firmware!')
                    return True
                elif any(indicator in line.lower() for indicator in ['failed', 'error']):
                    self.logger.error(f'Device reported: {line}')
                    return False

            time.sleep(0.1)

        self.logger.error(f'Timeout waiting for device ready ({timeout}s)')
        return False

    def wait_for_ready_signal(self, timeout: Optional[float] = None) -> bool:
        '''Wait for index\r\n ready signal from device.'''
        if not self.device:
            return False

        timeout = timeout or self.config.ready_signal_timeout
        start_time = time.time()
        buffer = b''

        while time.time() - start_time < timeout:
            if self.device.in_waiting > 0:
                data = self.device.read(self.device.in_waiting)
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
                            if self.debug:
                                self.logger.debug(f'Non-index data: {line!r}')
                    except UnicodeDecodeError:
                        if self.debug:
                            self.logger.debug(f'Binary data received: {line!r}')

            time.sleep(0.01)

        return False

    def calculate_checksum(self, data: bytes) -> int:
        '''Calculate XOR checksum for data, same as Lizard UART protocol.'''
        checksum = 0
        for byte in data:
            checksum ^= byte
        return checksum

    def send_firmware_chunk(self, chunk_data: bytes) -> None:
        '''Send a single firmware chunk with checksum.'''
        if not self.device:
            return

        checksum = self.calculate_checksum(chunk_data)
        self.device.write(chunk_data)
        checksum_suffix = OTAProtocol.CHECKSUM_SUFFIX_FORMAT.format(checksum).encode('ascii')
        self.device.write(checksum_suffix)
        self.device.flush()

    def wait_for_final_confirmation(self) -> bool:
        '''Wait for final OTA confirmation from device.'''
        if not self.device:
            return False

        self.logger.info('Listening for device OTA confirmation...')
        start_time = time.time()
        response_status = 'unknown'  # 'success', 'failure', or 'unknown'
        status_message = None

        while time.time() - start_time < self.config.device_response_timeout:
            if self.device.in_waiting > 0:
                try:
                    line = self.device.readline().decode(errors='replace').strip()
                    if line and not line.isspace():
                        if self.debug:
                            self.logger.debug(f'Device: {line}')

                        # Check for success conditions
                        lcline = line.lower()
                        if any(indicator in lcline for indicator in OTAProtocol.SUCCESS_INDICATORS):
                            response_status = 'success'
                            status_message = line
                            self.logger.success('Device confirmed OTA success!')
                            return True

                        # Check for failure conditions
                        if any(indicator in lcline for indicator in OTAProtocol.FAILURE_INDICATORS):
                            response_status = 'failure'
                            status_message = line
                            self.logger.failure(f'OTA Failed: {line}')
                            return False

                except UnicodeDecodeError:
                    continue

            time.sleep(0.1)

        # Handle timeout cases
        if response_status == 'success':
            self.logger.success('Transfer completed successfully!')
            return True
        elif response_status == 'failure':
            self.logger.failure(f'OTA Failed: {status_message}')
            return False
        else:
            self.logger.warning('Transfer completed - status unknown (no clear success/failure detected)')
            return True

    def send_firmware(self, firmware_path: str) -> bool:
        '''Send firmware binary using checksum-validated chunks.'''
        if not os.path.exists(firmware_path):
            self.logger.error(f'Firmware file not found: {firmware_path}')
            return False

        if not self.device:
            self.logger.error('No device connected')
            return False

        file_size = os.path.getsize(firmware_path)
        self.logger.info(f'Sending firmware: {firmware_path} ({file_size:,} bytes)')

        try:
            self.logger.success('Device is ready! Starting data transfer...')

            with open(firmware_path, 'rb') as firmware_file:
                bytes_sent = 0
                chunk_count = 0
                progress_bar = ProgressBar(file_size, self.config.progress_bar_width)

                if not self.wait_for_ready_signal(timeout=10):
                    self.logger.error('Device not ready for initial transfer')
                    return False

                while bytes_sent < file_size:
                    chunk_data = firmware_file.read(self.config.chunk_size)
                    if not chunk_data:
                        break

                    chunk_count += 1
                    self.send_firmware_chunk(chunk_data)
                    bytes_sent += len(chunk_data)

                    # Update progress bar
                    progress_bar.update(bytes_sent, chunk_count)

                    if bytes_sent < file_size:
                        if not self.wait_for_ready_signal(timeout=3):
                            self.logger.error(f'Device not ready for more data at {bytes_sent:,} bytes')
                            return False

                # Wait for final confirmation from device
                if not self.wait_for_ready_signal(timeout=3):
                    self.logger.error(f'Device not ready for final confirmation after {bytes_sent:,} bytes')
                    return False

                self.logger.success(f'Transfer complete: {bytes_sent:,} bytes sent in {chunk_count} chunks')

                return self.wait_for_final_confirmation()

        except Exception as e:
            self.logger.error(f'Error during transfer: {e}')
            return False

    def perform_ota(self, firmware_path: str, target: str) -> bool:
        '''Perform complete OTA update.'''
        self.logger.info('🦎 Starting Lizard UART OTA...')

        if not self.connect():
            return False

        try:
            ota_cmd = OTAProtocol.COMMAND_FORMAT.format(target)
            if not self.send_command(ota_cmd):
                return False

            if not self.wait_for_ready():
                return False

            if not self.send_firmware(firmware_path):
                return False

            return True

        finally:
            self.disconnect()


def parse_args() -> argparse.Namespace:
    '''Parse command line arguments.'''
    parser = argparse.ArgumentParser(description='Perform UART OTA update for Lizard firmware')

    parser.add_argument('firmware', help='Path to firmware binary file')
    parser.add_argument('-p', '--port', default=OTAConfig.port,
                        help=f'Serial port (default: {OTAConfig.port})')
    parser.add_argument('-b', '--baudrate', type=int, default=OTAConfig.baudrate,
                        help=f'Baud rate (default: {OTAConfig.baudrate})')
    parser.add_argument('-d', '--debug', action='store_true',
                        help='Debug output (shows ready signals and protocol details)')
    parser.add_argument('--target', default='core',
                        help='OTA target (e.g. core, p0, plexus, etc.; default: core)')

    return parser.parse_args()


def main() -> None:
    '''Main entry point.'''
    args = parse_args()

    config = OTAConfig(
        port=args.port,
        baudrate=args.baudrate
    )

    ota = LizardOTA(config, debug=args.debug)

    try:
        success = ota.perform_ota(args.firmware, args.target)
        sys.exit(0 if success else 1)

    except KeyboardInterrupt:
        OTALogger().failure('OTA interrupted')
        sys.exit(1)
    except Exception as e:
        OTALogger().failure(f'OTA failed: {e}')
        sys.exit(1)


if __name__ == "__main__":
    main()
