#!/usr/bin/env python3
import serial
import sys

if len(sys.argv) != 3:
    print(f'Usage: {sys.argv[0]} config_file device_path')
    exit()

txt_path, usb_path = sys.argv[1:]

def send(line):
    print(f'Sending: {line}')
    checksum = 0
    for c in line:
        checksum ^= ord(c)
    port.write((f'{line}@{checksum:02x}\n').encode())

with serial.Serial(usb_path, baudrate=115200, timeout=1.0) as port:
    send('!-')
    expander = False
    with open(txt_path) as f:
        for line in f.read().splitlines():
            if line == '---':
                expander = True
                send('!>!-')
            else:
                if expander:
                    send(f'!>!+{line}')
                else:
                    send(f'!+{line}')
    if expander:
        send('!>!.')
        send('!>core.restart()')
    send('!.')
    send('core.restart()')
