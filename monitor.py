#!/usr/bin/env python3
import argparse
import asyncio
import glob
import os

import serial
from prompt_toolkit import PromptSession
from prompt_toolkit.patch_stdout import patch_stdout
from serial.tools import list_ports

parser = argparse.ArgumentParser(description='Monitor an ESP32 running Lizard firmware')
parser.add_argument('device', nargs='?', help='Serial device path (e.g., /dev/ttyUSB0)')
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
    while True:
        # decode tolerantly so invalid bytes (e.g. noise or a baud mismatch) never crash the reader
        line = line_reader.readline().decode('utf-8', errors='replace').strip('\r\n')
        if line[-3:-2] == '@':
            try:
                check = int(line[-2:], 16)
            except ValueError:
                check = None
            if check is not None:
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


def find_devices() -> list[str]:
    usb_patterns = [
        '/dev/ttyTHS*',
        '/dev/ttyUSB*',
        '/dev/cu.usbserial-*',
        '/dev/cu.usbmodem*',
        '/dev/tty.SLAB_USBtoUART',
        '/dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_*',
    ]
    devices: list[str] = []
    seen: set[str] = set()
    for pattern in usb_patterns:
        for path in sorted(glob.glob(pattern)):
            real = os.path.realpath(path)
            if real not in seen:
                seen.add(real)
                devices.append(path)
    return devices


def describe(path: str) -> str:
    real = os.path.realpath(path)
    for port in list_ports.comports():
        if os.path.realpath(port.device) == real:
            return port.description or port.manufacturer or ''
    return ''


def serial_connection() -> serial.Serial:
    if args.device:
        usb_path = args.device
    else:
        devices = find_devices()
        if not devices:
            raise Exception('No serial port found')
        if len(devices) == 1:
            usb_path = devices[0]
        else:
            print('Multiple serial devices found:')
            for i, path in enumerate(devices):
                info = describe(path)
                print(f'  [{i}] {path}' + (f' ({info})' if info else ''))
            while True:
                choice = input(f'Select device [0-{len(devices) - 1}, default 0]: ').strip()
                if not choice:
                    usb_path = devices[0]
                    break
                if choice.isdigit() and int(choice) < len(devices):
                    usb_path = devices[int(choice)]
                    break
                print('Invalid selection.')

    print(f'Connecting to {usb_path} at {args.baud} baud')
    return serial.Serial(usb_path, baudrate=args.baud, timeout=0.1)


if __name__ == '__main__':
    with serial_connection() as port:
        loop = asyncio.get_event_loop_policy().get_event_loop()
        loop.create_task(send())
        loop.run_in_executor(None, receive)
        loop.run_forever()
