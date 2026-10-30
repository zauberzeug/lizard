#!/usr/bin/env python3
import argparse
import asyncio
import sys
import threading
import time

import serial
from prompt_toolkit import PromptSession
from prompt_toolkit.patch_stdout import patch_stdout

import telemetry
from serial_devices import resolve_device

parser = argparse.ArgumentParser(description='Monitor an ESP32 running Lizard firmware')
parser.add_argument('device', nargs='?', help='Serial device path (default: auto-detected, asks if ambiguous)')
parser.add_argument('--baud', type=int, default=115200, help='Baud rate (default: 115200)')
args = parser.parse_args()


class LineReader:
    # https://github.com/pyserial/pyserial/issues/216#issuecomment-369414522

    def __init__(self, s: serial.Serial) -> None:
        self.buf = bytearray()
        self.s = s

    def readline(self) -> bytearray:
        i = self.buf.find(b'\n')
        if i >= 0:
            r = self.buf[:i+1]
            self.buf = self.buf[i+1:]
            return r
        while True:
            i = max(1, min(2048, self.s.in_waiting))
            data = self.s.read(i)
            i = data.find(b'\n')
            if i >= 0:
                r = self.buf + data[:i+1]
                self.buf[0:] = data[i+1:]
                return r
            else:
                self.buf.extend(data)


def receive() -> None:
    line_reader = LineReader(port)
    decoder = telemetry.Decoder()
    unknown_versions = set()
    layout_activity = 0.0  # time of the last layout request or layout line
    while True:
        # decode tolerantly so invalid bytes (e.g. noise or a baud mismatch) never crash the reader
        line, check = telemetry.split_checksum(line_reader.readline().decode(errors='replace').strip('\r\n'))
        if check is not None and (actual := telemetry.checksum(line)) != check:
            print(f'ERROR: CHECKSUM MISMATCH ({actual} vs. {check} for "{line}")')
            print(line)  # never taken as a frame or a layout line
            continue
        result = decoder.feed(line)
        if isinstance(result, telemetry.Layout):
            layout_activity = time.monotonic()  # a new request would restart the layout lines still coming
        if not isinstance(result, telemetry.Frame):
            print(line)
            continue
        if result.status == 'unknown_version':
            if result.sender in unknown_versions:
                continue
            unknown_versions.add(result.sender)
        else:
            unknown_versions.discard(result.sender)  # a later switch to an unknown version shows its first frame again
        print(result if result.sender is None else f'bus[{result.sender}]: {result}')
        now = time.monotonic()
        if result.status in ('no_layout', 'layout_incomplete', 'layout_mismatch') and now > layout_activity + 2.0:
            write_line('core.telemetry_info()')  # e.g. the frames started before the monitor
            layout_activity = now


def write_line(text: str) -> None:
    with port_lock:
        port.write(f'{text}@{telemetry.checksum(text):02x}\n'.encode())


async def send() -> None:
    session = PromptSession()
    while True:
        try:
            with patch_stdout():
                line = await session.prompt_async('> ')
                for segment in line.split('\n'):
                    # drop the \r of a CRLF paste, which would otherwise end up inside the checksum payload
                    write_line(segment.rstrip('\r'))
        except (KeyboardInterrupt, EOFError):
            print('Bye!')
            loop.stop()
            return


def serial_connection() -> serial.Serial:
    device = resolve_device(args.device)
    print(f'Connecting to {device} at {args.baud} baud')
    return serial.Serial(device, baudrate=args.baud, timeout=0.1)


if __name__ == '__main__':
    try:
        connection = serial_connection()
    except (RuntimeError, serial.SerialException) as e:
        sys.exit(f'Error: {e}')
    port_lock = threading.Lock()  # the receiver asks for telemetry layouts while the prompt sends commands
    with connection as port:
        loop = asyncio.get_event_loop_policy().get_event_loop()
        loop.create_task(send())
        loop.run_in_executor(None, receive)
        loop.run_forever()
