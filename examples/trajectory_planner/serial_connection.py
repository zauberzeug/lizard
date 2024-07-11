from typing import Optional

import serial


class SerialConnection:

    def __init__(self, port: str = '/dev/tty.SLAB_USBtoUART', baudrate: int = 115200, timeout: float = 1.0) -> None:
        try:
            self.port = serial.Serial(port, baudrate=baudrate, timeout=timeout)
        except serial.SerialException:
            print(f'Device {port} is unavailable. Running without serial connection.')
            self.port = None
        self.buffer = ''

    def read(self) -> Optional[str]:
        if not self.port:
            return None
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
        return None

    def send(self, line: str) -> None:
        if not self.port:
            return
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
