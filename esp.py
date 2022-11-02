from contextlib import contextmanager
from typing import Optional, Union


class Esp:

    def __init__(self, nand: bool = False, xavier: bool = False, device: Optional[str] = None) -> None:
        print('Initializing ESP...')
        self.en = 434 if xavier else 216
        self.g0 = 428 if xavier else 50
        if device is None:
            self.device = '/dev/ttyTHS' + ('0' if xavier else '1')
        else:
            self.device = device
        self.on = 1 if nand else 0
        self.off = 0 if nand else 1

    def write(self, path: str, value: Union[str, int]) -> None:
        try:
            print(f'echo {value:3} > /sys/class/gpio/{path}')
            with open(f'/sys/class/gpio/{path}', 'w') as f:
                f.write(f'{value}\n')
        except:
            print(f'could not write {value} to {path}')

    @contextmanager
    def pin_config(self) -> None:
        print('Configuring pins...')
        self.write('export', self.en)
        self.write('export', self.g0)
        self.write(f'gpio{self.en}/direction', 'out')
        self.write(f'gpio{self.g0}/direction', 'out')
        yield
        self.write('unexport', self.en)
        self.write('unexport', self.g0)

    @contextmanager
    def flash_mode(self):
        print('Bringing microcontroller into flash mode...')
        self.write(f'gpio{self.en}/value', self.on)
        self.write(f'gpio{self.g0}/value', self.on)
        self.write(f'gpio{self.en}/value', self.off)
        yield
        self.activate()

    def activate(self) -> None:
        print('Bringing microcontroller into normal operation mode...')
        self.write(f'gpio{self.g0}/value', self.off)
        self.write(f'gpio{self.en}/value', self.on)
        self.write(f'gpio{self.en}/value', self.off)
