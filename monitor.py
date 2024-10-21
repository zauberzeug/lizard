#!/usr/bin/env python3
import asyncio
import os.path
import sys

import serial
from prompt_toolkit import PromptSession
from prompt_toolkit.patch_stdout import patch_stdout


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
    while True:
        try:
            line = line_reader.readline().decode('utf-8').strip('\r\n')
        except UnicodeDecodeError:
            print(f'ERROR: COULD NOT DECODE "{line}"')
        else:
            if line[-3:-2] == '@':
                check = int(line[-2:], 16)
                line = line[:-3]
                checksum = 0
                for c in line:
                    checksum ^= ord(c)
                if checksum != check:
                    print(f'ERROR: CHECKSUM MISMATCH ({checksum} vs. {check} for "{line}")')
            print(line)


async def send() -> None:
    session = PromptSession()
    while True:
        try:
            with patch_stdout():
                line = await session.prompt_async('> ') + '\n'
                checksum = 0
                start = 0
                for i, c in enumerate(line):
                    if c == '\n':
                        port.write(f'{line[start:i]}@{checksum:02x}\n'.encode('utf-8'))
                        checksum = 0
                        start = i
                    else:
                        checksum ^= ord(c)
        except (KeyboardInterrupt, EOFError):
            print('Bye!')
            loop.stop()
            return


def serial_connection() -> serial.Serial:
    if len(sys.argv) > 1:
        usb_path = sys.argv[1]
    else:
        usb_paths = [
            '/dev/ttyTHS0',
            '/dev/ttyTHS1',
            '/dev/ttyUSB0',
            '/dev/ttyUSB1',
            '/dev/tty.SLAB_USBtoUART',
            '/dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0',
        ]
        for usb_path in usb_paths:
            if os.path.exists(usb_path):
                break
        else:
            raise Exception('No serial port found')

    print(f'Connecting to {usb_path}')
    return serial.Serial(usb_path, baudrate=115200, timeout=0.1)


if __name__ == '__main__':
    with serial_connection() as port:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        loop.create_task(send())
        loop.run_in_executor(None, receive)
        loop.run_forever()
