"""Serial monitor: shows the lines of a microcontroller running Lizard and sends the commands typed at the prompt."""
import argparse
import asyncio
import sys
import threading
import time
from typing import List, Optional

import serial
from prompt_toolkit import PromptSession
from prompt_toolkit.patch_stdout import patch_stdout

from .console_line import checksum, format_line, split_checksum
from .serial_devices import resolve_device
from .telemetry import Frame, LayoutRequester, TelemetryDecoder


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


class Monitor:

    def __init__(self, port: serial.Serial) -> None:
        self.port = port
        self.port_lock = threading.Lock()  # the receiver asks for telemetry layouts while the prompt sends commands
        self.loop = asyncio.new_event_loop()

    def receive(self) -> None:
        line_reader = LineReader(self.port)
        decoder = TelemetryDecoder()
        layout_requester = LayoutRequester()
        unknown_versions = set()
        while True:
            # decode tolerantly so invalid bytes (e.g. noise or a baud mismatch) never crash the reader
            line, check = split_checksum(line_reader.readline().decode(errors='replace'))
            if check is not None and (actual := checksum(line)) != check:
                print(f'ERROR: CHECKSUM MISMATCH ({actual} vs. {check} for "{line}")')
                print(line)  # never taken as a frame or a layout line
                continue
            result = decoder.feed(line)
            if layout_requester.feed(result, time.monotonic()):
                self.write_line('core.telemetry_info()')  # e.g. the frames started before the monitor
            if not isinstance(result, Frame):
                print(line)
                continue
            if result.status == 'unknown_version':
                if result.sender in unknown_versions:
                    continue
                unknown_versions.add(result.sender)
            else:
                # a later switch to an unknown version shows its first frame again
                unknown_versions.discard(result.sender)
            print(result if result.sender is None else f'{result.sender}: {result}')

    def write_line(self, text: str) -> None:
        with self.port_lock:
            self.port.write(format_line(text).encode())

    async def send(self) -> None:
        session: PromptSession = PromptSession()
        while True:
            try:
                with patch_stdout():
                    line = await session.prompt_async('> ')
                    for segment in line.split('\n'):
                        # drop the \r of a CRLF paste, which would otherwise end up inside the checksum payload
                        self.write_line(segment.rstrip('\r'))
            except (KeyboardInterrupt, EOFError):
                print('Bye!')
                self.loop.stop()
                return

    def run(self) -> None:
        asyncio.set_event_loop(self.loop)
        self.loop.create_task(self.send())
        self.loop.run_in_executor(None, self.receive)
        self.loop.run_forever()


def main(argv: Optional[List[str]] = None) -> None:
    parser = argparse.ArgumentParser(description='Monitor an ESP32 running Lizard firmware')
    parser.add_argument('device', nargs='?', help='Serial device path (default: auto-detected, asks if ambiguous)')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate (default: 115200)')
    args = parser.parse_args(argv)
    try:
        device = resolve_device(args.device)
        print(f'Connecting to {device} at {args.baud} baud')
        connection = serial.Serial(device, baudrate=args.baud, timeout=0.1)
    except (RuntimeError, serial.SerialException) as e:
        sys.exit(f'Error: {e}')
    with connection as port:
        Monitor(port).run()


if __name__ == '__main__':
    main()
