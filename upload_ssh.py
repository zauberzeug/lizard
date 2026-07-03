#!/usr/bin/env python3
"""Copy Lizard's build/ to a remote host over SSH and flash it via espresso.py.

This wrapper previously called the now-removed flash.py. flash.py's positional
vocabulary (nano/xavier/orin/nand/v05/usb/enable/reset/-e) is deliberately NOT
carried over: espresso.py is Orin-only and flag-driven, so a faithful one-to-one
translation is not possible. Rather than forward those tokens (which would fail
with an opaque error on the remote, or mis-flash), passing any of them fails fast
below with an explanation and the migration. The guard can be removed a release or
two after 0.13.0, once the old muscle-memory has faded.
"""
import argparse
import subprocess

parser = argparse.ArgumentParser(description='Upload Lizard to a remote host and flash to a microcontroller.')
parser.add_argument('host', type=str, help='user@host or user@host:~/path/to/lizard')
parser.add_argument('flash_args', nargs=argparse.REMAINDER,
                    help='arguments passed through to "espresso.py flash" '
                         '(e.g. --nand, --swap, --chip esp32s3, --device /dev/ttyUSB0)')
args = parser.parse_args()

# flash.py is gone; reject its old positional vocabulary loudly (with the reason and the
# migration) instead of forwarding it to espresso.py, which does not understand it. Scan
# token-aware: skip the value consumed by espresso's value-taking flags, so a legitimate
# value that happens to equal a legacy word (e.g. "--firmware nand") is not misread, and
# also catch the old bare "/dev/X" device form (which flash.py accepted, espresso does not).
_LEGACY_WORDS = {'nano', 'xavier', 'orin', 'nand', 'v05', 'usb', 'enable', 'reset', '-e', '--erase'}
_VALUE_FLAGS = {'--device', '--firmware', '--bootloader', '--partition-table', '--chip'}
_legacy_used = []
_skip_value = False
for _tok in args.flash_args:
    if _skip_value:
        _skip_value = False  # this token is the previous flag's value, not a positional
    elif _tok in _VALUE_FLAGS:
        _skip_value = True
    elif _tok.startswith('--') and '=' in _tok:
        continue  # "--flag=value" carries its own value
    elif _tok in _LEGACY_WORDS or _tok.startswith('/dev/'):
        _legacy_used.append(_tok)
if _legacy_used:
    parser.error(
        f'legacy flash.py argument(s) {_legacy_used} are no longer accepted; '
        'this wrapper now runs "espresso.py flash", which is flag-driven. Migrate:\n'
        '  nano | xavier   not supported -- espresso.py is Orin-only (the per-board GPIO\n'
        '                  maps that flash.py carried were dropped with it)\n'
        '  orin | v05      drop them -- the Jetson is auto-detected and piggyboard v0.5+ is the default\n'
        '  nand            -> --nand\n'
        '  usb | /dev/X    -> --device /dev/X\n'
        '  enable | reset  separate espresso.py commands; run them directly (this wrapper only flashes)\n'
        '  -e | --erase    run "espresso.py erase" (a separate command) before flashing\n'
        '  e.g.  upload_ssh.py user@host --nand --device /dev/ttyTHS0'
    )

host, *path = args.host.split(':')
target = path[0] if path else '~/lizard'

print(f'Copying binary files to {host}:{target}...')
command = f'rsync -zarv --prune-empty-dirs --include="*/" --include="*.bin" --exclude="*" build {host}:{target}'
subprocess.run(command, shell=True, check=True)

print('Flashing microcontroller...')
command = f'''ssh -t {host} "bash --login -c 'cd {target} && sudo ./espresso.py flash {" ".join(args.flash_args)}'"'''
subprocess.run(command, shell=True, check=True)
