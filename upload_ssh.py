#!/usr/bin/env python3
import argparse
import subprocess

parser = argparse.ArgumentParser(description='Upload Lizard to a remote host and flash to a microcontroller.')
parser.add_argument('host', type=str, help='user@host or user@host:~/path/to/lizard')
parser.add_argument('flash_args', type=str, nargs='*', help='flash arguments')
args = parser.parse_args()
host, *path = args.host.split(':')
target = path[0] if path else '~/lizard'

print(f'Copying binary files to {host}:{target}...')
command = f'rsync -zarv --prune-empty-dirs --include="*/" --include="*.bin" --exclude="*" build {host}:{target}'
subprocess.run(command, shell=True, check=True)

print('Flashing microcontroller...')
command = f'''ssh -t {host} "bash --login -c 'cd {target} && sudo ./flash.py {" ".join(args.flash_args)}'"'''
subprocess.run(command, shell=True, check=True)
