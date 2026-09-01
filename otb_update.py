#!/usr/bin/env python3
import argparse
import base64
import re
import sys
import time
import zlib
from pathlib import Path

import serial

CHUNK_SIZE = 165  # must match BUS_OTB_CHUNK_SIZE in main/utils/otb.h (the chunk line has to fit the bus payload)
WINDOW = 8  # must match BUS_OTB_WINDOW in main/utils/otb.h
ACK_TIMEOUT = 2.0  # resend the window when no ack arrives for this long
STALL_TIMEOUT = 15.0  # give up when the target makes no progress for this long (its own session timeout is 10 s)

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


class OtbTimeout(OtbError):
    pass


def read_line() -> str:
    line = dev.readline().decode(errors='ignore')
    if '__OTB_ERROR__' in line:
        raise OtbError(line.strip())
    return line


def wait_ack(prefix: str, timeout: float = 10.0) -> str:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if prefix in (line := read_line()):
            return line
    raise OtbTimeout(f'no {prefix} within {timeout:.0f} s')


def transact(msg: str, ack: str = '') -> str:
    dev.write(f'{args.bus}.send({args.target},"{msg}")\n'.encode())
    return wait_ack(ack) if ack else ''


def send_chunks(data: bytes, use_crc: bool) -> int:
    """Sliding window with go-back-N retransmission; returns the number of resent chunks.

    The target re-acks its last written chunk (or __OTB_ACK_BEGIN__ while nothing is written) on any gap,
    duplicate or corrupted chunk, so a lost frame in either direction rewinds the window instead of aborting.
    """
    ack_pattern = re.compile(r'__OTB_ACK_CHUNK_(\d+)__')
    acked = -1  # highest chunk the target has confirmed written
    sent = -1  # last chunk we pushed out
    resends = 0
    stale_until = -2  # after a rewind the target keeps re-acking the stale window; those must not rewind again
    last_ack_at = last_progress_at = time.time()

    def chunk_line(seq: int) -> str:
        chunk = data[seq * CHUNK_SIZE:(seq + 1) * CHUNK_SIZE]
        b64 = base64.b64encode(chunk).decode()
        if not use_crc:
            return f'__OTB_CHUNK_{seq}__:{b64}'
        crc = zlib.crc32(chunk, zlib.crc32(str(seq).encode()))  # covers the offset, not just the bytes
        return f'__OTB_CHUNK_{seq}__:{crc:08x}:{b64}'

    def rewind() -> None:
        nonlocal sent, resends, stale_until, last_ack_at
        resends += sent - acked
        stale_until = sent
        sent = acked
        last_ack_at = time.time()

    while acked < number_of_chunks - 1:
        while sent - acked < WINDOW and sent < number_of_chunks - 1:
            sent += 1
            transact(chunk_line(sent))
        line = read_line()
        now = time.time()
        n = None
        if match := ack_pattern.search(line):
            n = int(match.group(1))
        elif '__OTB_ACK_BEGIN__' in line:
            n = -1
        if n is not None and acked < n <= sent:
            acked = n
            last_ack_at = last_progress_at = now
            if acked % 50 == 0 or acked == number_of_chunks - 1:
                print(f'\rSending chunk {acked + 1}/{number_of_chunks} ({resends} resends)...', end='')
        elif n is not None and n == acked and sent > acked and n > stale_until:
            rewind()  # duplicate ack: the chunk after it got lost
        elif now - last_ack_at > ACK_TIMEOUT and sent > acked:
            rewind()  # nothing came back: resend the whole window
        if now - last_progress_at > STALL_TIMEOUT:
            raise OtbError(f'no progress for {STALL_TIMEOUT:.0f} s')
    return resends


try:
    if args.expander:
        print('Pausing broadcasts on expander...')
        dev.write(f'{args.expander}.pause_broadcasts()\n'.encode())
        dev.flush()

    print(f'Starting OTB to node {args.target} ({file_size} bytes)...')
    started = time.time()
    for attempt in range(3):  # the begin frame can fall into a still-booting target
        try:
            begin_ack = transact('__OTB_BEGIN__', '__OTB_ACK_BEGIN__')
            break
        except OtbTimeout:
            if attempt == 2:
                raise
    use_crc = '__OTB_ACK_BEGIN__:crc32' in begin_ack  # older targets neither advertise nor accept the CRC field
    if not use_crc:
        print('Target does not support chunk CRCs, relying on the image checksum only.')

    resends = send_chunks(firmware.read_bytes(), use_crc)
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
