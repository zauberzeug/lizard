#!/usr/bin/env python3
import argparse
import os
import subprocess
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(description='Build ESP project for different targets')
    parser.add_argument('target', choices=['esp32', 'esp32s3'], nargs='?', default='esp32',
                        help='Target chip type (default: esp32)')
    parser.add_argument('--clean', action='store_true',
                        help='Clean build directory before building')
    return parser.parse_args()


def main():
    args = parse_args()

    # Set IDF target and SDKCONFIG_DEFAULTS
    os.environ['IDF_TARGET'] = args.target

    # Set the appropriate sdkconfig defaults file
    sdkconfig_defaults_file = f'sdkconfig.defaults.{args.target}'
    if not Path(sdkconfig_defaults_file).exists():
        print(f"Error: {sdkconfig_defaults_file} not found!")
        return 1

    os.environ['SDKCONFIG_DEFAULTS'] = sdkconfig_defaults_file
    print(f"Using target: {args.target}")
    print(f"Using config: {sdkconfig_defaults_file}")

    # Clean if requested
    if args.clean:
        print("Running full clean...")
        subprocess.run(['idf.py', 'fullclean'], check=False)

        # Remove existing sdkconfig to ensure clean configuration
        for config in ['sdkconfig', 'sdkconfig.old']:
            if Path(config).exists():
                print(f"Removing {config}")
                Path(config).unlink()

    # Build
    print("Building...")
    result = subprocess.run(['idf.py', 'build'], check=False)
    if result.returncode != 0:
        print("Build failed!")
        return result.returncode

    print(f"Build completed successfully for {args.target}")
    return 0


if __name__ == '__main__':
    exit(main())
