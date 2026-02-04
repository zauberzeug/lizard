#!/usr/bin/env python3
import argparse
import base64
import sys
import time
from pathlib import Path

import serial

CHUNK_SIZE = 174

parser = argparse.ArgumentParser(description='Push firmware via SerialBus OTB')
parser.add_argument('firmware', help='Path to firmware binary')
parser.add_argument('--port', default='/dev/ttyUSB0', help='Serial port')
parser.add_argument('--baud', type=int, default=115200, help='Baudrate')
parser.add_argument('--target', type=int, required=True, help='Bus ID of target node')
parser.add_argument('--bus', default='bus', help='SerialBus module name')
parser.add_argument('--expander', help='Expander to pause broadcasts on')
args = parser.parse_args()

firmware = Path(args.firmware)
if not firmware.exists():
    sys.exit(f'Firmware not found: {firmware}')

file_size = firmware.stat().st_size
number_of_chunks = (file_size + CHUNK_SIZE - 1) // CHUNK_SIZE

try:
    dev = serial.Serial(args.port, args.baud, timeout=0.5)
except serial.SerialException as e:
    sys.exit(f'Serial error: {e}')


class OtbError(Exception):
    pass


def transact(message: str, expected_ack: str = '', *, timeout: float = 10.0) -> None:
    """Send command and wait for ACK (if expected_ack is given). Raise OtbError on failure."""
    dev.write(f'{args.bus}.send({args.target},"{message}")\n'.encode())
    dev.flush()
    if not expected_ack:
        return
    deadline = time.time() + timeout
    while time.time() < deadline:
        if raw := dev.readline():
            line = raw.decode(errors='ignore')
            word = line.split()[1 if line.startswith(b'otb[') else 0]  # handle lines starting with "otb[<sender_id>] "
            if word.startswith('__OTB_ERROR__'):
                raise OtbError(line)
            if word.startswith(expected_ack):
                return
    raise OtbError('Timeout waiting for response')


try:
    if args.expander:
        print('Pausing broadcasts on expander...')
        dev.write(f'{args.expander}.pause_broadcasts()\n'.encode())
        dev.flush()

    print(f'Starting OTB to node {args.target} ({file_size} bytes)...')
    started = time.time()
    transact('__OTB_BEGIN__', '__OTB_ACK_BEGIN__')

    seq = 0
    with firmware.open('rb') as fh:
        while chunk := fh.read(CHUNK_SIZE):
            base64_chunk = base64.b64encode(chunk).decode()
            print(f'\rSending chunk {seq} of {number_of_chunks}...', end='')
            transact(f'__OTB_CHUNK_{seq}__:{base64_chunk}', f'__OTB_ACK_CHUNK_{seq}__')
            seq += 1

    print('\nCommitting image...')
    transact('__OTB_COMMIT__', '__OTB_ACK_COMMIT__')

    print(f'Transfer finished in {time.time() - started:.1f}s, restarting node...')
    transact('core.restart()')

except OtbError as e:
    print(f'\nTransmission failed: {e}')
    transact('__OTB_ABORT__')
    sys.exit(1)

except KeyboardInterrupt:
    print('\nInterrupted')
    sys.exit(1)

finally:
    if args.expander:
        print('Resuming broadcasts on expander...')
        dev.write(f'{args.expander}.resume_broadcasts()\n'.encode())
        dev.flush()
    dev.close()
