#!/usr/bin/env python3
"""Deprecated: remote flashing now lives in espresso.py.

Kept as a one-release pointer. Use espresso.py's --host instead, which rsyncs the
binaries plus espresso.py itself and runs the command on the remote:

    ./espresso.py flash --host user@host[:path] [--nand] [--device /dev/ttyTHS0] ...

--host works for every command (flash/erase/enable/disable/reset/coredump), so remote
recovery workflows the old wrapper only hinted at are now first-class.
"""
import sys

print(__doc__, file=sys.stderr)
sys.exit(2)
