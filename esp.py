from contextlib import contextmanager
from typing import Union

class Esp:

    def __init__(self, nand=False, xavier=False, device=None) ->None:
        print('Initializing ESP...')
        self.en=434 if xavier else 216
        self.g0=428 if xavier else 50
        if device is None:
            self.device = '/dev/ttyTHS' + ('0' if xavier else '1')
        else:
            self.device = device
        self.on=1 if nand else 0
        self.off=0 if nand else 1

    def write(self,value:Union[str,int], path:str):
        try:
            with open(f'/sys/class/gpio{path}', 'w') as f:
                f.write(str(value) + '\n')
        except:
            print(f'could not write {value} to {path}')

    @contextmanager
    def pin_config(self):
        print('Configuring pins...')
        self.write(self.en, '/export')
        self.write(self.g0, '/export')
        self.write('out', f'/gpio{self.en}/direction')
        self.write('out', f'/gpio{self.g0}/direction')
        yield
        self.write(self.en, '/unexport')
        self.write(self.g0, '/unexport')

    def activate(self):
        print('Bringing microcontroller into normal operation mode...')
        self.write(self.off, f'/gpio{self.g0}/value')
        self.write(self.on, f'/gpio{self.en}/value')
        self.write(self.off, f'/gpio{self.en}/value')

    @contextmanager
    def flash_mode(self):
        print('Bringing microcontroller into flash mode...')
        self.write(self.on, f'/gpio{self.en}/value')
        self.write(self.on, f'/gpio{self.g0}/value')
        self.write(self.off, f'/gpio{self.en}/value')
        yield
        self.activate()
