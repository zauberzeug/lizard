#!/usr/bin/env python3
import argparse
import base64
import os
import sys
import time
from typing import Iterable, Optional

import serial


DEFAULT_CHUNK_SIZE = 174
READY_TIMEOUT = 10.0
ACK_TIMEOUT = 8.0
DONE_TIMEOUT = 15.0
DEFAULT_WINDOW = 12


def strip_checksum(line: str) -> str:
    return line.split('@', 1)[0].strip()


def unwrap_status(line: str) -> Optional[str]:
    clean = strip_checksum(line)

    # Lines coming through an expander are prefixed (e.g. "p0: ota[1] ..."), so
    # look for the first OTA marker and drop anything before it.
    for marker in ('ota[', '__OTA_'):
        pos = clean.find(marker)
        if pos >= 0:
            clean = clean[pos:]
            break

    if clean.startswith('ota[') and '] ' in clean:
        clean = clean.split('] ', 1)[1]
    return clean if clean.startswith('__OTA_') else None


def wait_for_status(dev: serial.Serial, prefixes: Iterable[str], timeout: float) -> Optional[str]:
    deadline = time.time() + timeout
    while time.time() < deadline:
        raw = dev.readline()
        if not raw:
            continue
        try:
            decoded = raw.decode(errors='ignore')
        except UnicodeDecodeError:
            continue

        status = unwrap_status(decoded)
        if not status:
            continue

        if status.startswith('__OTA_ERROR__'):
            return status
        for prefix in prefixes:
            if status.startswith(prefix):
                return status
    return None


def send_bus_command(dev: serial.Serial, target: int, payload: str) -> None:
    dev.write(f'bus.send({target},"{payload}")\n'.encode())
    dev.flush()


def send_command(dev: serial.Serial, command: str) -> None:
    dev.write(f"{command}\n".encode())
    dev.flush()


def parse_ready_status(status: str, default_chunk: int) -> int:
    try:
        parts = status.split(':')
        if len(parts) >= 3:
            return min(default_chunk, int(parts[2]))
    except ValueError:
        pass
    return default_chunk


def perform_ota(dev: serial.Serial,
                firmware_path: str,
                target: int,
                chunk_size: int,
                window: int,
                expander: Optional[str]) -> bool:
    file_size = os.path.getsize(firmware_path)
    print(f"Starting OTA to node {target} ({file_size} bytes)...", flush=True)

    if expander:
        send_command(dev, f'{expander}.run("core.pause_broadcasts()")')
        # Give the expander a moment to process and clear any buffered chatter.
        time.sleep(0.2)
        dev.reset_input_buffer()

    try:
        send_bus_command(dev, target, f"{'__OTA_BEGIN__'}:{file_size}")
        ready = wait_for_status(dev, ['__OTA_READY__'], READY_TIMEOUT)
        if not ready:
            print('No ready response from device', flush=True)
            return False
        if ready.startswith('__OTA_ERROR__'):
            print(f"Device rejected OTA: {ready}", flush=True)
            return False

        chunk_size = parse_ready_status(ready, chunk_size)
        print(f"Device ready, using chunk size {chunk_size}", flush=True)

        seq = 1
        sent = 0
        expected_ack = 1
        outstanding = 0
        started = time.time()
        with open(firmware_path, 'rb') as fh:
            finished_sending = False
            while not finished_sending or outstanding > 0:
                if not finished_sending and outstanding < window:
                    chunk = fh.read(chunk_size)
                    if not chunk:
                        finished_sending = True
                        continue
                    payload = f"{'__OTA_CHUNK__'}:{seq}:{base64.b64encode(chunk).decode()}"
                    send_bus_command(dev, target, payload)
                    outstanding += 1
                    seq += 1
                    sent += len(chunk)
                    continue

                ack_status = wait_for_status(dev, ['__OTA_ACK__', '__OTA_ERROR__'], ACK_TIMEOUT)
                if not ack_status or ack_status.startswith('__OTA_ERROR__'):
                    print(f"OTA failed waiting for ack on chunk {expected_ack}: {ack_status}", flush=True)
                    send_bus_command(dev, target, '__OTA_ABORT__')
                    return False

                try:
                    parts = ack_status.split(':')
                    ack_seq = int(parts[1])
                    ack_bytes = int(parts[2]) if len(parts) > 2 else sent
                except (ValueError, IndexError):
                    print(f"Malformed ack from device: {ack_status}", flush=True)
                    send_bus_command(dev, target, '__OTA_ABORT__')
                    return False

                if ack_seq != expected_ack:
                    print(f"Out-of-order ack: expected {expected_ack}, got {ack_seq}", flush=True)
                    send_bus_command(dev, target, '__OTA_ABORT__')
                    return False

                expected_ack += 1
                outstanding -= 1
                progress = int(ack_bytes * 100 / file_size)
                print(f"\rSent {ack_bytes}/{file_size} bytes ({progress}%)", end='', flush=True)

        print('\nCommitting image...', flush=True)
        send_bus_command(dev, target, '__OTA_COMMIT__')
        done = wait_for_status(dev, ['__OTA_DONE__'], DONE_TIMEOUT)
        if not done or done.startswith('__OTA_ERROR__'):
            print(f"OTA did not complete cleanly: {done}", flush=True)
            return False

        duration = time.time() - started
        print(f"OTA finished in {duration:.1f}s", flush=True)
        return True
    finally:
        if expander:
            send_command(dev, f'{expander}.run("core.resume_broadcasts()")')


def main() -> int:
    parser = argparse.ArgumentParser(description='Push firmware to a bus peer via SerialBus OTA')
    parser.add_argument('firmware', help='Path to firmware binary')
    parser.add_argument('--port', default='/dev/ttyUSB0', help='Serial port connected to the coordinator')
    parser.add_argument('--baud', type=int, default=115200, help='Baudrate for the coordinator port')
    parser.add_argument('--id', type=int, required=True, help='Bus ID of the target node')
    parser.add_argument('--chunk-size', type=int, default=DEFAULT_CHUNK_SIZE, help='Override chunk size (<=174)')
    parser.add_argument('--window', type=int, default=DEFAULT_WINDOW, help='Outstanding chunk window (<=32)')
    parser.add_argument('--expander', help='Expander name to pause broadcasts on (e.g. p0) during OTA')
    args = parser.parse_args()

    if args.expander and not args.expander.isidentifier():
        print(f"Invalid expander name: {args.expander}", flush=True)
        return 1

    if args.chunk_size > DEFAULT_CHUNK_SIZE or args.chunk_size <= 0:
        print(f"Invalid chunk size; using default {DEFAULT_CHUNK_SIZE}", flush=True)
        args.chunk_size = DEFAULT_CHUNK_SIZE
    if args.window <= 0 or args.window > 32:
        print(f"Invalid window; using default {DEFAULT_WINDOW}", flush=True)
        args.window = DEFAULT_WINDOW
    elif args.window > 12:
        print('Warning: large windows can stall polled buses; try 8-12 if you see timeouts', flush=True)

    try:
        with serial.Serial(args.port, args.baud, timeout=0.5) as dev:
            dev.reset_input_buffer()
            dev.reset_output_buffer()
            success = perform_ota(dev, args.firmware, args.id, args.chunk_size, args.window, args.expander)
            return 0 if success else 1
    except FileNotFoundError:
        print(f"Firmware not found: {args.firmware}", flush=True)
        return 1
    except serial.SerialException as exc:
        print(f"Serial error: {exc}", flush=True)
        return 1
    except KeyboardInterrupt:
        print('Interrupted', flush=True)
        return 1


if __name__ == '__main__':
    sys.exit(main())
