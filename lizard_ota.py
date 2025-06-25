#!/usr/bin/env python3
import argparse
import serial
import sys
import os
import time


def parse_args():
    parser = argparse.ArgumentParser(description='Perform UART OTA update for Lizard firmware')

    parser.add_argument('firmware', nargs='?', help='Path to firmware binary file')
    parser.add_argument('-p', '--port', default='/dev/tty.SLAB_USBtoUART',
                        help='Serial port (default: /dev/tty.SLAB_USBtoUART)')
    parser.add_argument('-b', '--baudrate', type=int, default=115200,
                        help='Baud rate (default: 115200)')
    parser.add_argument('-t', '--timeout', type=int, default=30,
                        help='OTA timeout in seconds (default: 30)')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Verbose output')
    parser.add_argument('--validate', action='store_true',
                        help='Run in validation mode (listen for validation requests)')

    return parser.parse_args()


def log(message, verbose=False, force=False):
    """Log message if verbose or force is True."""
    if verbose or force:
        print(f"[OTA] {message}")


def connect_serial(port, baudrate, verbose=False):
    """Connect to serial port."""
    try:
        ser = serial.Serial(port=port, baudrate=baudrate, timeout=5)
        log(f"Connected to {port} at {baudrate} baud", verbose, force=True)
        return ser
    except Exception as e:
        print(f"Error: Failed to connect to {port}: {e}")
        return None


def send_ota_command(ser, verbose=False):
    """Send core.ota() command and wait for response."""
    try:
        log("Sending core.ota() command...", verbose, force=True)
        ser.write(b"core.ota()\n")

        # Wait for response
        start_time = time.time()
        while time.time() - start_time < 10:
            if ser.in_waiting > 0:
                response = ser.readline().decode().strip()
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


def wait_for_ready(ser, timeout, verbose=False):
    """Wait for device to be ready for firmware transfer."""
    log("Waiting for device to be ready...", verbose, force=True)
    start_time = time.time()

    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            line = ser.readline().decode().strip()
            log(f"Device: {line}", verbose, force=True)  # Always show device messages

            if "Waiting for firmware transfer to begin" in line:
                log("Device ready for firmware!", verbose, force=True)
                return True
            elif "failed" in line.lower() or "error" in line.lower():
                print(f"Error: Device reported: {line}")
                return False

        time.sleep(0.1)

    print(f"Error: Timeout waiting for device ready ({timeout}s)")
    return False


def wait_for_ready_signal(ser, timeout=2):
    """Wait for ###OTA_READY### signal from device."""
    start_time = time.time()

    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            line = ser.readline().decode().strip()
            log(f"Device: {line}", False, True)  # Always show device messages

            if "###OTA_READY###" in line:
                return True
            elif "failed" in line.lower() or "error" in line.lower():
                print(f"Error: Device reported: {line}")
                return False

        time.sleep(0.1)

    print(f"Timeout waiting for ready signal ({timeout}s)")
    return False


def send_firmware(ser, firmware_path, verbose=False):
    """Send firmware binary using UART OTA protocol."""
    if not os.path.exists(firmware_path):
        print(f"Error: Firmware file not found: {firmware_path}")
        return False

    file_size = os.path.getsize(firmware_path)
    print(f"Sending firmware: {firmware_path} ({file_size} bytes)")

    try:
        # Send start marker with size
        start_cmd = f"###OTA_START###{file_size}\n"
        print(f"Sending start command: {start_cmd.strip()}")  # Always show this
        ser.write(start_cmd.encode())
        ser.flush()  # Ensure data is sent immediately
        log("Start command sent successfully", verbose, force=True)

        # Small delay to ensure ESP32 processes the start command
        time.sleep(0.5)

        # Wait for first ready signal
        print("Waiting for device ready signal...")
        if not wait_for_ready_signal(ser):
            return False

            # Send firmware data in chunks
        print("Starting chunked firmware transfer...")
        chunk_size = 512   # 512 byte chunks for better speed

        print(f"Using {chunk_size} byte chunks")

        with open(firmware_path, 'rb') as f:
            bytes_sent = 0
            chunk_num = 1

            while bytes_sent < file_size:
                # Read chunk from file
                remaining = file_size - bytes_sent
                current_chunk_size = min(chunk_size, remaining)
                chunk_data = f.read(current_chunk_size)

                if not chunk_data:
                    break

                    # Send this chunk (256 bytes, optimal for ESP32)
                print(f"Sending chunk {chunk_num} ({current_chunk_size} bytes)...")
                ser.write(chunk_data)
                ser.flush()
                bytes_sent += len(chunk_data)

                # Minimal delay for 256-byte chunks
                time.sleep(0.002)  # 2ms delay - very fast

                progress = int((bytes_sent / file_size) * 100)
                # Only print progress every 5% to reduce I/O overhead
                if progress % 5 == 0 and progress != getattr(send_firmware, 'last_progress', -1):
                    print(f"Progress: {progress}% ({bytes_sent}/{file_size} bytes)")
                    send_firmware.last_progress = progress

                chunk_num += 1

                # Wait for ready signal for next chunk (unless we're done)
                if bytes_sent < file_size:
                    print("Waiting for device ready for next chunk...")
                    if not wait_for_ready_signal(ser):
                        print("Device not ready for next chunk")
                        return False

                # Send end marker
        ser.write(b"###OTA_END###")
        log("Sent end marker", verbose)

        print(f"Transfer complete: {bytes_sent} bytes sent")

        # Listen for device response after transfer
        print("Listening for device response...")
        start_time = time.time()
        while time.time() - start_time < 120:  # Listen for 60 seconds
            if ser.in_waiting > 0:
                line = ser.readline().decode().strip()
                print(f"Device: {line}")

                if "OTA completed successfully" in line or "Rebooting" in line:
                    print("✅ Device confirmed OTA success!")
                    return True
                elif "OTA failed" in line or "failed" in line.lower():
                    print("❌ Device reported OTA failure!")
                    return False

            time.sleep(0.1)

        print("⏰ No device response received after transfer")
        return True  # Assume success if no error reported

    except Exception as e:
        print(f"Error during transfer: {e}")
        return False


def listen_for_validation(port, baudrate, timeout, verbose=False):
    """Listen for OTA validation requests and respond."""
    print("🔍 Starting OTA validation listener...")

    ser = connect_serial(port, baudrate, verbose)
    if not ser:
        return False

    try:
        log("Listening for validation requests...", verbose, force=True)
        start_time = time.time()

        while time.time() - start_time < timeout:
            if ser.in_waiting > 0:
                line = ser.readline().decode().strip()
                log(f"Device: {line}", verbose)

                # Check for validation request
                if line.startswith("OTA_VALIDATE:"):
                    version = line.split(":", 1)[1] if ":" in line else "unknown"
                    log(f"Validation request for version: {version}", verbose, force=True)

                    # Send validation response
                    ser.write(b"OTA_VALID\n")
                    log("Sent validation confirmation", verbose, force=True)
                    print("✅ OTA validation completed successfully!")
                    return True

            time.sleep(0.1)

        print(f"⏰ Validation timeout ({timeout}s) - no validation request received")
        return False

    finally:
        ser.close()
        log("Disconnected", verbose)


def perform_ota(port, baudrate, firmware_path, timeout, verbose=False):
    """Perform complete OTA update."""
    print("🦎 Starting Lizard UART OTA...")

    # Connect to serial
    ser = connect_serial(port, baudrate, verbose)
    if not ser:
        return False

    try:
        # Send OTA command
        if not send_ota_command(ser, verbose):
            return False

        # Wait for device ready
        if not wait_for_ready(ser, timeout, verbose):
            return False

        # Send firmware
        if not send_firmware(ser, firmware_path, verbose):
            return False

        print("✅ OTA completed successfully!")
        print("Device should reboot with new firmware.")
        return True

    finally:
        ser.close()
        log("Disconnected", verbose)


def main():
    args = parse_args()

    # Validation mode
    if args.validate:
        try:
            success = listen_for_validation(
                args.port,
                args.baudrate,
                args.timeout,
                args.verbose
            )
            sys.exit(0 if success else 1)

        except KeyboardInterrupt:
            print("\n❌ Validation interrupted")
            sys.exit(1)
        except Exception as e:
            print(f"❌ Validation failed: {e}")
            sys.exit(1)

    # OTA mode (requires firmware file)
    if not args.firmware:
        print("Error: Firmware file required for OTA mode")
        print("Use --validate flag for validation mode")
        sys.exit(1)

    try:
        success = perform_ota(
            args.port,
            args.baudrate,
            args.firmware,
            args.timeout,
            args.verbose
        )
        sys.exit(0 if success else 1)

    except KeyboardInterrupt:
        print("\n❌ OTA interrupted")
        sys.exit(1)
    except Exception as e:
        print(f"❌ OTA failed: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
