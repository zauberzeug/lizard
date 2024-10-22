#!/usr/bin/env python3
import argparse
from esp_coredump import CoreDump

# can be standalone with https://github.com/espressif/esp-coredump?tab=readme-ov-file#standalone-installation-without-esp-idf

parser = argparse.ArgumentParser(description=(
    'ESP32 Core Dump Utility using esp_coredump. '
    'If no options are provided, only the core dump info will be shown. '
    'Use --debug to start a debugging session and exit to return to the shell.'
))
parser.add_argument('-p', '--port', default='/dev/ttyUSB0', help='Serial port (default: /dev/ttyUSB0)')
parser.add_argument('-b', '--baud', default='115200', help='Baud rate (default: 115200)')
parser.add_argument('-e', '--elf', default='build/lizard.elf', help='Path to ELF file (default: build/lizard.elf)')
parser.add_argument('--debug', action='store_true', help='Start a debug session')

args = parser.parse_args()

coredump = CoreDump(chip='esp32', port=args.port, baud=int(args.baud), prog=args.elf)

if args.debug:
    coredump.dbg_corefile()
else:
    coredump.info_corefile()
