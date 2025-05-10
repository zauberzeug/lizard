from contextlib import contextmanager
from time import sleep
from typing import Optional, Union, Literal, Generator
from pathlib import Path


class Esp:

    def __init__(self,
                 *,
                 jetson: Literal['nano', 'xavier', 'orin'] = 'nano',
                 nand: bool = False,
                 v05: bool = False,
                 device: Optional[str] = None,
                 ) -> None:
        print('Initializing ESP...')
        self.en = {
            'nano': 216,
            'xavier': 436,
            'orin': 492,
        }[jetson]
        self.g0 = {
            'nano': 50,
            'xavier': 428,
            'orin': 460,
        }[jetson]
        self.gpio_en = 'PAC.06' if jetson == 'orin' else f'gpio{self.en}'
        self.gpio_g0 = 'PR.04' if jetson == 'orin' else f'gpio{self.g0}'

        if jetson == 'orin' and v05:
            self.en, self.g0 = self.g0, self.en
            self.gpio_en, self.gpio_g0 = self.gpio_g0, self.gpio_en

        self.device = device or {
            'nano': '/dev/ttyTHS1',
            'xavier': '/dev/ttyTHS0',
            'orin': '/dev/ttyTHS0',
        }[jetson]
        self.on = 1 if nand else 0
        self.off = 0 if nand else 1

    def write(self, path: str, value: Union[str, int]) -> None:
        print(f'echo {value:3} > /sys/class/gpio/{path}')
        Path(f'/sys/class/gpio/{path}').write_text(f'{value}\n', encoding='utf-8')

    @contextmanager
    def pin_config(self) -> Generator[None, None, None]:
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
        self.reset()

    def reset(self) -> None:
        print('Resetting microcontroller...')
        self.write(f'{self.gpio_g0}/value', self.off)
        sleep(0.5)
        self.write(f'{self.gpio_en}/value', self.on)
        sleep(0.5)
        self.write(f'{self.gpio_en}/value', self.off)
        print('reset complete')

    def enable(self) -> None:
        print('Enabling microcontroller...')
        self.write(f'{self.gpio_g0}/value', self.off)
        sleep(0.5)
        self.write(f'{self.gpio_en}/value', self.off)
        print('enable complete')

    def disable(self) -> None:
        print('Disabling microcontroller...')
        self.write(f'{self.gpio_en}/value', self.on)
        print('disable complete')
