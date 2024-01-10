from contextlib import contextmanager
from time import sleep
from typing import Optional, Union


class Esp:

    def __init__(self,
                 nand: bool = False,
                 xavier: bool = False,
                 orin: bool = False,
                 device: Optional[str] = None,
                 ) -> None:
        print('Initializing ESP...')
        self.en = 436 if xavier else 492 if orin else 216
        self.g0 = 428 if xavier else 460 if orin else 50
        self.gpio_en = f'gpio{self.en}' if not orin else 'PAC.06'
        self.gpio_g0 = f'gpio{self.g0}' if not orin else 'PR.04'
        if device is None:
            self.device = '/dev/ttyTHS' + ('0' if xavier else '0' if orin else '1')
        else:
            self.device = device
        self.on = 1 if nand else 0
        self.off = 0 if nand else 1

    def write(self, path: str, value: Union[str, int]) -> None:
        try:
            print(f'echo {value:3} > /sys/class/gpio/{path}')
            with open(f'/sys/class/gpio/{path}', 'w') as f:
                f.write(f'{value}\n')
        except Exception:
            print(f'could not write {value} to {path}')

    @contextmanager
    def pin_config(self) -> None:
        print('Configuring pins...')
        self.write('export', self.en)
        sleep(0.5)
        self.write('export', self.g0)
        sleep(0.5)
        self.write(f'{self.gpio_en}/direction', 'out')
        sleep(0.5)
        self.write(f'{self.gpio_g0}/direction', 'out')
        sleep(0.5)
        yield
        self.write('unexport', self.en)
        sleep(0.5)
        self.write('unexport', self.g0)

    @contextmanager
    def flash_mode(self):
        print('Bringing microcontroller into flash mode...')
        self.write(f'{self.gpio_en}/value', self.on)
        sleep(0.5)
        self.write(f'{self.gpio_g0}/value', self.on)
        sleep(0.5)
        self.write(f'{self.gpio_en}/value', self.off)
        sleep(0.5)
        yield
        self.activate()

    def activate(self) -> None:
        print('Bringing microcontroller into normal operation mode...')
        self.write(f'{self.gpio_g0}/value', self.off)
        sleep(0.5)
        self.write(f'{self.gpio_en}/value', self.on)
        sleep(0.5)
        self.write(f'{self.gpio_en}/value', self.off)
