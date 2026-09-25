#!/usr/bin/env python3
import argparse
import asyncio
import os.path

import serial
from prompt_toolkit import PromptSession
from prompt_toolkit.patch_stdout import patch_stdout

parser = argparse.ArgumentParser(description='Monitor an ESP32 running Lizard firmware')
parser.add_argument('device', nargs='?', help='Serial device path (e.g., /dev/ttyUSB0)')
parser.add_argument('--baud', type=int, default=115200, help='Baud rate (default: 115200)')
args = parser.parse_args()


class StreamReader:
    """Split the console stream into text lines and binary telemetry frames.

    A frame travels as 0x00 | COBS(body) | 0x00: after a 0x00, a 0x01 opens a frame and the next 0x00 closes it;
    anything else after a 0x00 is text again. Text lines end with a newline.
    """

    def __init__(self, s: serial.Serial) -> None:
        self.s = s
        self.text = bytearray()
        self.frame = bytearray()
        self.state = 'text'
        self.items: list[tuple[str, bytes]] = []

    def read(self) -> tuple[str, bytes]:
        while not self.items:
            self.feed(self.s.read(max(1, min(2048, self.s.in_waiting))))
        return self.items.pop(0)

    def feed(self, data: bytes) -> None:
        for byte in data:
            if self.state == 'frame':
                if byte == 0:
                    self.items.append(('frame', bytes(self.frame)))
                    self.frame.clear()
                    self.state = 'boundary'
                else:
                    self.frame.append(byte)
            elif self.state == 'boundary':
                if byte == 0:
                    continue
                if byte == 1:
                    self.frame.append(byte)
                    self.state = 'frame'
                    continue
                self.state = 'text'
                self.text.clear()
                if byte == 10:
                    self.items.append(('text', b''))
                else:
                    self.text.append(byte)
            elif byte == 0:
                self.text.clear()
                self.state = 'boundary'
            elif byte == 10:
                self.items.append(('text', bytes(self.text)))
                self.text.clear()
            else:
                self.text.append(byte)


def cobs_decode(data: bytes) -> bytes | None:
    out = bytearray()
    i = 0
    while i < len(data):
        code = data[i]
        i += 1
        if code == 0 or i + code - 1 > len(data):
            return None
        out += data[i:i + code - 1]
        i += code - 1
        if code < 0xff and i < len(data):
            out.append(0)
    return bytes(out)


def crc16(data: bytes) -> int:
    crc = 0xffff
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xffff if crc & 0x8000 else (crc << 1) & 0xffff
    return crc


def describe_frame(encoded: bytes) -> str:
    """One line per frame: header fields and the payload as hex, or the raw bytes if it does not verify."""
    body = cobs_decode(encoded)
    if body is None or len(body) < 11 or body[0] != 0:
        return f'[corrupt frame: {encoded.hex()}]'
    src, frame_id, seq, millis, length = body[1], body[2], body[3], int.from_bytes(body[4:8], 'little'), body[8]
    if length != len(body) - 11 or crc16(body[:-2]) != int.from_bytes(body[-2:], 'little'):
        return f'[corrupt frame: {encoded.hex()}]'
    return f'[frame src={src} id={frame_id} seq={seq} millis={millis} payload={body[9:-2].hex()}]'


def receive() -> None:
    reader = StreamReader(port)
    while True:
        kind, data = reader.read()
        if kind == 'frame':
            print(describe_frame(data))
            continue
        # decode tolerantly so invalid bytes (e.g. noise or a baud mismatch) never crash the reader
        line = data.decode(errors='replace').strip('\r\n')
        if line[-3:-2] == '@':
            try:
                check = int(line[-2:], 16)
            except ValueError:
                check = None
            if check is not None:
                line = line[:-3]
                checksum = 0
                for byte in line.encode():
                    checksum ^= byte
                if checksum != check:
                    print(f'ERROR: CHECKSUM MISMATCH ({checksum} vs. {check} for "{line}")')
        print(line)


async def send() -> None:
    session = PromptSession()
    while True:
        try:
            with patch_stdout():
                line = await session.prompt_async('> ')
                for segment in line.split('\n'):
                    # drop the \r of a CRLF paste, which would otherwise end up inside the checksum payload
                    segment = segment.rstrip('\r')
                    checksum = 0
                    for byte in segment.encode():
                        checksum ^= byte
                    port.write(f'{segment}@{checksum:02x}\n'.encode())
        except (KeyboardInterrupt, EOFError):
            print('Bye!')
            loop.stop()
            return


def serial_connection() -> serial.Serial:
    if args.device:
        usb_path = args.device
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

    print(f'Connecting to {usb_path} at {args.baud} baud')
    return serial.Serial(usb_path, baudrate=args.baud, timeout=0.1)


if __name__ == '__main__':
    with serial_connection() as port:
        loop = asyncio.get_event_loop_policy().get_event_loop()
        loop.create_task(send())
        loop.run_in_executor(None, receive)
        loop.run_forever()
