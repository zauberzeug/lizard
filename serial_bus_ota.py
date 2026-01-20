#!/usr/bin/env python3
import argparse
import base64
import os
import sys
import time
from typing import Iterable, Optional

import serial

DEFAULT_CHUNK_SIZE = 162
DEFAULT_WINDOW = 12
READY_TIMEOUT = 10.0
ACK_TIMEOUT = 8.0
DONE_TIMEOUT = 15.0


def send(dev: serial.Serial, line: str) -> None:
    dev.write(f'{line}\n'.encode())
    dev.flush()


def wait_for_status(dev: serial.Serial, prefixes: Iterable[str], timeout: float) -> Optional[str]:
    deadline = time.time() + timeout
    while time.time() < deadline:
        raw = dev.readline()
        if not raw:
            continue

        # Strip checksum and find OTA marker (may be prefixed by expander name)
        clean = raw.decode(errors='ignore').split('@', 1)[0].strip()
        for marker in ('ota[', '__OTA_'):
            pos = clean.find(marker)
            if pos >= 0:
                clean = clean[pos:]
                break
        if clean.startswith('ota[') and '] ' in clean:
            clean = clean.split('] ', 1)[1]
        if not clean.startswith('__OTA_'):
            continue

        if clean.startswith('__OTA_ERROR__') or any(clean.startswith(p) for p in prefixes):
            return clean
    return None


def perform_ota(dev: serial.Serial, firmware_path: str, target: int,
                chunk_size: int, window: int, expander: Optional[str]) -> bool:
    file_size = os.path.getsize(firmware_path)
    print(f'Starting OTA to node {target} ({file_size} bytes)...', flush=True)

    if expander:
        send(dev, f'{expander}.run("core.pause_broadcasts()")')
        time.sleep(0.2)
        dev.reset_input_buffer()

    try:
        send(dev, f'bus.send({target},"__OTA_BEGIN__:{file_size}")')
        ready = wait_for_status(dev, ['__OTA_READY__'], READY_TIMEOUT)
        if not ready or ready.startswith('__OTA_ERROR__'):
            print(f'Device rejected OTA: {ready}', flush=True)
            return False

        # Parse chunk size from ready response
        parts = ready.split(':')
        if len(parts) >= 3:
            chunk_size = min(chunk_size, int(parts[2]))
        print(f'Device ready, using chunk size {chunk_size}', flush=True)

        seq, expected_ack, outstanding = 1, 1, 0
        started = time.time()
        with open(firmware_path, 'rb') as fh:
            done_sending = False
            while not done_sending or outstanding > 0:
                while not done_sending and outstanding < window:
                    chunk = fh.read(chunk_size)
                    if not chunk:
                        done_sending = True
                        break
                    send(dev, f'bus.send({target},"__OTA_CHUNK__:{seq}:{base64.b64encode(chunk).decode()}")')
                    outstanding += 1
                    seq += 1

                if outstanding == 0:
                    break

                ack = wait_for_status(dev, ['__OTA_ACK__'], ACK_TIMEOUT)
                if not ack or ack.startswith('__OTA_ERROR__'):
                    print(f'\nOTA failed waiting for ack {expected_ack}: {ack}', flush=True)
                    send(dev, f'bus.send({target},"__OTA_ABORT__")')
                    return False

                try:
                    ack_parts = ack.split(':')
                    ack_seq, ack_bytes = int(ack_parts[1]), int(ack_parts[2])
                except (ValueError, IndexError):
                    print(f'\nMalformed ack: {ack}', flush=True)
                    send(dev, f'bus.send({target},"__OTA_ABORT__")')
                    return False

                if ack_seq != expected_ack:
                    print(f'\nOut-of-order ack: expected {expected_ack}, got {ack_seq}', flush=True)
                    send(dev, f'bus.send({target},"__OTA_ABORT__")')
                    return False

                expected_ack += 1
                outstanding -= 1
                print(f'\rSent {ack_bytes}/{file_size} bytes ({ack_bytes * 100 // file_size}%)', end='', flush=True)

        print('\nCommitting image...', flush=True)
        send(dev, f'bus.send({target},"__OTA_COMMIT__")')
        done = wait_for_status(dev, ['__OTA_DONE__'], DONE_TIMEOUT)
        if not done or done.startswith('__OTA_ERROR__'):
            print(f'OTA did not complete cleanly: {done}', flush=True)
            return False

        print(f'OTA finished in {time.time() - started:.1f}s', flush=True)
        return True
    finally:
        if expander:
            send(dev, f'{expander}.run("core.resume_broadcasts()")')


def main() -> int:
    parser = argparse.ArgumentParser(description='Push firmware to a bus peer via SerialBus OTA')
    parser.add_argument('firmware', help='Path to firmware binary')
    parser.add_argument('--port', default='/dev/ttyUSB0', help='Serial port')
    parser.add_argument('--baud', type=int, default=115200, help='Baudrate')
    parser.add_argument('--id', type=int, required=True, help='Bus ID of target node')
    parser.add_argument('--chunk-size', type=int, default=DEFAULT_CHUNK_SIZE, help='Chunk size (<=174)')
    parser.add_argument('--window', type=int, default=DEFAULT_WINDOW, help='Outstanding chunks (<=32)')
    parser.add_argument('--expander', help='Expander name to pause broadcasts on during OTA')
    args = parser.parse_args()

    if args.expander and not args.expander.isidentifier():
        print(f'Invalid expander name: {args.expander}', flush=True)
        return 1

    chunk_size = min(max(1, args.chunk_size), DEFAULT_CHUNK_SIZE)
    window = min(max(1, args.window), 32)

    try:
        with serial.Serial(args.port, args.baud, timeout=0.5) as dev:
            dev.reset_input_buffer()
            return 0 if perform_ota(dev, args.firmware, args.id, chunk_size, window, args.expander) else 1
    except FileNotFoundError:
        print(f'Firmware not found: {args.firmware}', flush=True)
        return 1
    except serial.SerialException as exc:
        print(f'Serial error: {exc}', flush=True)
        return 1
    except KeyboardInterrupt:
        print('Interrupted', flush=True)
        return 1


if __name__ == '__main__':
    sys.exit(main())
