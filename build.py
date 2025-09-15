#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description='Build ESP project for different targets')
    parser.add_argument('target', choices=['esp32_internal', 'plexus', 'plexus_mini'], nargs='?', default='esp32_internal',
                        help='Hardware target (default: esp32_internal)')
    parser.add_argument('--clean', action='store_true',
                        help='Clean build directory before building')
    args = parser.parse_args()

    sdkconfig_defaults_file = f'sdkconfig.defaults.{args.target}'
    if not Path(sdkconfig_defaults_file).exists():
        print(f'Error: {sdkconfig_defaults_file} not found!')
        return 1

    # Map esp-idf IDF_TARGET for hardware targets
    if args.target == 'esp32_internal':
        idf_target = 'esp32'
    else:
        idf_target = 'esp32s3'

    os.environ['IDF_TARGET'] = idf_target
    os.environ['SDKCONFIG_DEFAULTS'] = sdkconfig_defaults_file
    print(f'Using target: {args.target}')
    print(f'Using config: {sdkconfig_defaults_file}')

    if args.clean:
        print('Running full clean...')
        subprocess.run(['idf.py', 'fullclean'], check=False)
        for config in ['sdkconfig', 'sdkconfig.old']:
            if Path(config).exists():
                print(f'Removing {config}')
                Path(config).unlink()

    print('Building...')
    result = subprocess.run(['idf.py', 'build'], check=False)
    if result.returncode != 0:
        print('Build failed!')
        return result.returncode

    print(f'Build completed successfully for {args.target}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
