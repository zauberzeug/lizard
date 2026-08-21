#!/usr/bin/env python3
import argparse
import base64
import re
import sys
import time
import zlib
from pathlib import Path

import serial

CHUNK_SIZE = 165  # with the CRC field the line must stay within the bus payload (cap: BUS_OTB_CHUNK_SIZE)
WINDOW = 8  # must match BUS_OTB_WINDOW in main/utils/otb.h
ACK_TIMEOUT = 2.0  # resend the window when no ack arrives for this long
PACE = 0.01  # seconds between chunk writes: a saturated coordinator console tears incoming lines apart

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


def wait_ack(prefix: str, timeout: float = 10.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if raw := dev.readline():
            line = raw.decode(errors='ignore')
            if '__OTB_ERROR__' in line:
                raise OtbError(line)
            if prefix in line:
                return
    raise OtbError('Timeout')


def transact(msg: str, ack: str = '') -> None:
    dev.write(f'{args.bus}.send({args.target},"{msg}")\n'.encode())
    if ack:
        wait_ack(ack)


try:
    if args.expander:
        print('Pausing broadcasts on expander...')
        dev.write(f'{args.expander}.pause_broadcasts()\n'.encode())
        dev.flush()

    print(f'Starting OTB to node {args.target} ({file_size} bytes)...')
    started = time.time()
    for attempt in range(3):  # the begin frame can fall into a still-booting target
        try:
            transact('__OTB_BEGIN__', '__OTB_ACK_BEGIN__')
            break
        except OtbError as e:
            if attempt == 2 or 'Timeout' not in str(e):
                raise

    # Sliding window with go-back-N retransmission: the receiver re-acks its last written chunk
    # on any gap or duplicate, so a lost frame (either direction) rewinds instead of aborting.
    data = firmware.read_bytes()
    ack_pattern = re.compile(r'__OTB_ACK_CHUNK_(\d+)__')
    acked = -1  # highest chunk the receiver has confirmed written
    sent = -1   # last chunk we pushed out
    resends = 0
    recovering = False  # one rewind per loss: the stale window keeps dup-acking, ignore those
    last_ack_at = time.time()
    while acked < number_of_chunks - 1:
        while sent - acked < WINDOW and sent < number_of_chunks - 1:
            sent += 1
            chunk = data[sent * CHUNK_SIZE:(sent + 1) * CHUNK_SIZE]
            transact(f'__OTB_CHUNK_{sent}__:{zlib.crc32(chunk):08x}:{base64.b64encode(chunk).decode()}')
            time.sleep(PACE)
        raw = dev.readline()
        if not raw:
            if time.time() - last_ack_at > ACK_TIMEOUT:
                resends += sent - acked
                sent = acked  # nothing came back: resend the whole window
                recovering = True
                last_ack_at = time.time()
            continue
        line = raw.decode(errors='ignore')
        if '__OTB_ERROR__' in line:
            raise OtbError(line)
        if '__OTB_ACK_BEGIN__' in line and acked < 0 and not recovering:
            resends += sent + 1
            sent = -1  # chunk 0 got lost before anything was written
            recovering = True
            last_ack_at = time.time()
            continue
        if match := ack_pattern.search(line):
            n = int(match.group(1))
            last_ack_at = time.time()
            if n > acked:
                acked = n
                recovering = False
            elif n == acked and sent > acked and not recovering:
                resends += sent - acked
                sent = acked  # duplicate ack: the chunk after it got lost, go back
                recovering = True
            if acked % 50 == 0 or acked == number_of_chunks - 1:
                print(f'\rSending chunk {acked + 1}/{number_of_chunks} ({resends} resends)...', end='')
    print(f'\rSent {number_of_chunks}/{number_of_chunks} chunks ({resends} resends).      ')

    print('Committing image...')
    transact('__OTB_COMMIT__', '__OTB_ACK_COMMIT__')

    print(f'Transfer finished in {time.time() - started:.1f}s, restarting node...')
    transact('core.restart()')

except OtbError as e:
    print(f'\nTransmission failed: {e}')
    transact('__OTB_ABORT__')
    sys.exit(1)

except KeyboardInterrupt:
    print('\nInterrupted')
    transact('__OTB_ABORT__')
    sys.exit(1)

finally:
    if args.expander:
        print('Resuming broadcasts on expander...')
        dev.write(f'{args.expander}.resume_broadcasts()\n'.encode())
        dev.flush()
    dev.close()
