"""Write a startup script to a microcontroller running Lizard, directly or as a peer of a serial bus."""
import argparse
import logging
import sys
from pathlib import Path
from typing import List, Optional

import serial

from .connection import Connection
from .procedures import CORE, StartupError, Target, UnsupportedFirmware
from .serial_devices import resolve_device


def main(argv: Optional[List[str]] = None) -> None:
    parser = argparse.ArgumentParser(description='Configure an ESP32 running Lizard firmware')
    parser.add_argument('config_file', help='Path to the .liz configuration file')
    parser.add_argument('device_path', nargs='?',
                        help='Serial device path (default: auto-detected, asks if ambiguous)')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate (default: 115200)')
    parser.add_argument('--serial-bus', type=int, metavar='NODE_ID',
                        help='Send configuration via serial bus to the specified node ID')
    parser.add_argument('--bus-name', default='bus',
                        help='Name of the SerialBus module (default: bus)')
    args = parser.parse_args(argv)
    logging.basicConfig(format='%(levelname)s: %(message)s')

    script = Path(args.config_file).read_text('utf-8')
    target = CORE if args.serial_bus is None else Target.peer(args.bus_name, args.serial_bus)
    try:
        device = resolve_device(args.device_path)
        print(f'Connecting to {device} at {args.baud} baud')
        port = serial.Serial(device, baudrate=args.baud, timeout=0.1)
    except (RuntimeError, serial.SerialException) as e:
        sys.exit(f'Error: {e}')
    with port:
        connection = Connection(port, on_send=lambda line: print(f'Sending: {line}'))
        try:
            connection.write_startup(script, target)
        except (StartupError, UnsupportedFirmware, ValueError) as e:
            sys.exit(f'Error: {e}')
    print(f'The startup script of {target} matches {args.config_file}.')


if __name__ == '__main__':
    main()
