#!/usr/bin/env python3
import serial
from nicegui import ui

STARTUP = '''
can = Can(32, 33, 1000000)

rmd1 = RmdMotor(can, 1)
rmd2 = RmdMotor(can, 2)
rmd1.ratio = 9
rmd2.ratio = 9

core.output('core.millis rmd1.position rmd2.position')
'''

WHITE = '#FFFFFF'
BLUE = '#6E93D6'
GRAY = '#3A3E42'
SILVER = '#F8F8F8'
GREEN = '#53B689'
BLACK = '#111B1E'

ui.colors(primary=BLUE, secondary=GREEN)

port = serial.Serial('/dev/tty.SLAB_USBtoUART', baudrate=115200, timeout=1.0)


def send(line) -> None:
    print(f'Sending: {line}')
    checksum = 0
    for c in line:
        checksum ^= ord(c)
    port.write((f'{line}@{checksum:02x}\n').encode())


def configure() -> None:
    send('!-')
    for line in STARTUP.splitlines():
        send(f'!+{line}')
    send('!.')
    send('core.restart()')


with ui.header().style(f'background-color: {BLUE}').classes('justify-between items-center'):
    ui.label('RMD Motor Synchronization').classes('text-lg font-medium')
    ui.button('Configure', on_click=configure).props(f'flat color={WHITE}')

x = ui.number()
y = ui.number()


def submit() -> None:
    line = input.value
    checksum = 0
    for c in line:
        checksum ^= ord(c)
    port.write(f'{line}@{checksum:02x}\n'.encode('utf-8'))
    input.value = ''


input = ui.input('Lizard Command', on_change=submit)


def read() -> None:
    s = port.read_all()
    s = s.decode()
    if not s:
        return
    line = s.split('@', 1)[0]
    words = line.split()
    if len(words) != 4:
        return
    try:
        x.value = int(words[2])
        y.value = int(words[3])
    except ValueError:
        pass


ui.timer(0.01, read)
ui.on_startup(configure)
ui.on_shutdown(port.close)

ui.run()
