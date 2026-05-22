#!/usr/bin/env python3
import asyncio
import socket
import sys

from prompt_toolkit import PromptSession
from prompt_toolkit.patch_stdout import patch_stdout


class LineReader:

    def __init__(self, s: socket.socket) -> None:
        self.buf = bytearray()
        self.s = s

    def readline(self) -> bytearray:
        i = self.buf.find(b'\n')
        if i >= 0:
            r = self.buf[:i+1]
            self.buf = self.buf[i+1:]
            return r
        while True:
            data = self.s.recv(2048)
            if not data:
                raise ConnectionError('connection closed by peer')
            i = data.find(b'\n')
            if i >= 0:
                r = self.buf + data[:i+1]
                self.buf[0:] = data[i+1:]
                return r
            else:
                self.buf.extend(data)


def receive() -> None:
    line_reader = LineReader(sock)
    while True:
        try:
            line = line_reader.readline().decode('utf-8').strip('\r\n')
        except UnicodeDecodeError:
            print('ERROR: COULD NOT DECODE LINE')
            continue
        except ConnectionError as e:
            print(f'ERROR: {e}')
            loop.stop()
            return
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
                        sock.sendall(f'{line[start:i]}@{checksum:02x}\n'.encode('utf-8'))
                        checksum = 0
                        start = i
                    else:
                        checksum ^= ord(c)
        except (KeyboardInterrupt, EOFError):
            print('Bye!')
            loop.stop()
            return


def tcp_connection() -> socket.socket:
    if len(sys.argv) > 2:
        host = sys.argv[1]
        port = int(sys.argv[2])
    elif len(sys.argv) > 1 and ':' in sys.argv[1]:
        host, port_str = sys.argv[1].rsplit(':', 1)
        port = int(port_str)
    else:
        raise SystemExit(f'usage: {sys.argv[0]} <host> <port>   or   {sys.argv[0]} <host:port>')

    print(f'Connecting to {host}:{port}')
    s = socket.create_connection((host, port))
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return s


if __name__ == '__main__':
    with tcp_connection() as sock:
        loop = asyncio.get_event_loop_policy().get_event_loop()
        loop.create_task(send())
        loop.run_in_executor(None, receive)
        loop.run_forever()
