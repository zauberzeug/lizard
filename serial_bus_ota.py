#!/usr/bin/env python3
import argparse
import base64
import sys
import time
from pathlib import Path

import serial

CHUNK_SIZE = 174
WINDOW = 12
BEGIN_TIMEOUT = 10.0
ACK_TIMEOUT = 8.0
COMMIT_TIMEOUT = 15.0


def send(dev: serial.Serial, line: str) -> None:
    dev.write(f'{line}\n'.encode())
    dev.flush()


def wait_for_status(dev: serial.Serial, prefix: str, timeout: float) -> str | None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        raw = dev.readline()
        if not raw:
            continue

        clean = raw.decode(errors='ignore').split('@', 1)[0].strip()
        for marker in ('otb[', '__OTB_'):
            pos = clean.find(marker)
            if pos >= 0:
                clean = clean[pos:]
                break
        if clean.startswith('otb[') and '] ' in clean:
            clean = clean.split('] ', 1)[1]
        if not clean.startswith('__OTB_'):
            continue

        if clean.startswith('__OTB_ERROR__') or clean.startswith(prefix):
            return clean
    return None


def perform_otb(dev: serial.Serial, firmware: Path, target: int, bus: str, expander: str | None) -> bool:
    file_size = firmware.stat().st_size
    print(f'Starting OTB to node {target} ({file_size} bytes)...')

    if expander:
        send(dev, f'{expander}.run("core.pause_broadcasts()")')
        time.sleep(0.2)
        dev.reset_input_buffer()

    try:
        send(dev, f'{bus}.send({target},"__OTB_BEGIN__")')
        ready = wait_for_status(dev, '__OTB_ACK__', BEGIN_TIMEOUT)
        if not ready or ready.startswith('__OTB_ERROR__'):
            print(f'Device rejected OTB: {ready}')
            return False

        chunk_size = CHUNK_SIZE
        parts = ready.split(':')
        if len(parts) >= 3:
            chunk_size = min(chunk_size, int(parts[2]))
        print(f'Device ready, chunk size {chunk_size}')

        seq, expected_ack, outstanding = 1, 1, 0
        started = time.time()
        with open(firmware, 'rb') as fh:
            done_sending = False
            while not done_sending or outstanding > 0:
                while not done_sending and outstanding < WINDOW:
                    chunk = fh.read(chunk_size)
                    if not chunk:
                        done_sending = True
                        break
                    send(dev, f'{bus}.send({target},"__OTB_CHUNK__:{seq}:{base64.b64encode(chunk).decode()}")')
                    outstanding += 1
                    seq += 1

                if outstanding == 0:
                    break

                ack = wait_for_status(dev, '__OTB_ACK__', ACK_TIMEOUT)
                if not ack or ack.startswith('__OTB_ERROR__'):
                    print(f'\nOTB failed waiting for ack {expected_ack}: {ack}')
                    send(dev, f'{bus}.send({target},"__OTB_ABORT__")')
                    return False

                try:
                    ack_parts = ack.split(':')
                    ack_seq, ack_bytes = int(ack_parts[1]), int(ack_parts[2])
                except (ValueError, IndexError):
                    print(f'\nMalformed ack: {ack}')
                    send(dev, f'{bus}.send({target},"__OTB_ABORT__")')
                    return False

                if ack_seq != expected_ack:
                    print(f'\nOut-of-order ack: expected {expected_ack}, got {ack_seq}')
                    send(dev, f'{bus}.send({target},"__OTB_ABORT__")')
                    return False

                expected_ack += 1
                outstanding -= 1
                print(f'\rSent {ack_bytes}/{file_size} bytes ({ack_bytes * 100 // file_size}%)', end='')

        print('\nCommitting image...')
        send(dev, f'{bus}.send({target},"__OTB_COMMIT__")')
        done = wait_for_status(dev, '__OTB_ACK__', COMMIT_TIMEOUT)
        if not done or done.startswith('__OTB_ERROR__'):
            print(f'OTB did not complete cleanly: {done}')
            return False

        print(f'OTB finished in {time.time() - started:.1f}s, restarting node...')
        send(dev, f'{bus}.send({target},"core.restart()")')
        return True
    finally:
        if expander:
            send(dev, f'{expander}.run("core.resume_broadcasts()")')


def main() -> int:
    parser = argparse.ArgumentParser(description='Push firmware to a bus peer via SerialBus OTB (Over The Bus)')
    parser.add_argument('firmware', help='Path to firmware binary')
    parser.add_argument('--port', default='/dev/ttyUSB0', help='Serial port')
    parser.add_argument('--baud', type=int, default=115200, help='Baudrate')
    parser.add_argument('--id', type=int, required=True, help='Bus ID of target node')
    parser.add_argument('--bus', default='bus', help='Name of the SerialBus module (default: bus)')
    parser.add_argument('--expander', help='Expander name to pause broadcasts on during OTB')
    args = parser.parse_args()

    if args.expander and not args.expander.isidentifier():
        print(f'Invalid expander name: {args.expander}')
        return 1

    if not args.bus.isidentifier():
        print(f'Invalid bus name: {args.bus}')
        return 1

    firmware = Path(args.firmware)
    if not firmware.exists():
        print(f'Firmware not found: {firmware}')
        return 1

    try:
        with serial.Serial(args.port, args.baud, timeout=0.5) as dev:
            dev.reset_input_buffer()
            return 0 if perform_otb(dev, firmware, args.id, args.bus, args.expander) else 1
    except serial.SerialException as exc:
        print(f'Serial error: {exc}')
        return 1
    except KeyboardInterrupt:
        print('Interrupted')
        return 1


if __name__ == '__main__':
    sys.exit(main())
