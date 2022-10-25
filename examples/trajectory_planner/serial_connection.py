from typing import Optional

import serial


class SerialConnection:

    def __init__(self) -> None:
        self.port = serial.Serial('/dev/tty.SLAB_USBtoUART', baudrate=115200, timeout=1.0)
        self.buffer = ''

    def read(self) -> Optional[str]:
        s = self.port.read_all()
        s = s.decode()
        self.buffer += s
        if '\n' in self.buffer:
            line, self.buffer = self.buffer.split('\r\n', 1)
            if line[-3:-2] == '@':
                check = int(line[-2:], 16)
                line = line[:-3]
                checksum = 0
                for c in line:
                    checksum ^= ord(c)
                if checksum == check:
                    return line
            else:
                return line

    def send(self, line: str) -> None:
        print(f'Sending: {line}')
        checksum = 0
        for c in line:
            checksum ^= ord(c)
        self.port.write((f'{line}@{checksum:02x}\n').encode())

    def configure(self, startup_code: str) -> None:
        self.send('!-')
        for line in startup_code.splitlines():
            self.send(f'!+{line}')
        self.send('!.')
        self.send('core.restart()')
