#!/usr/bin/env python3
import subprocess
import argparse

parser = argparse.ArgumentParser(description=(
    'ESP32 Core Dump Utility using idf.py. '
    'If no options are provided, only the core dump info will be shown. '
    'Use --debug to start a debugging session.'
))
parser.add_argument('-p', '--port', default='/dev/ttyUSB0', help='Serial port (default: /dev/ttyUSB0)')
parser.add_argument('-b', '--baud', default='115200', help='Baud rate (default: 115200)')
parser.add_argument('--debug', action='store_true', help='Start a debug session')

args = parser.parse_args()
action = 'debug' if args.debug else 'info'

subprocess.run(['idf.py', f'coredump-{action}', '-p', args.port, '-b', args.baud], check=True)
