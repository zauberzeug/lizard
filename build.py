#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description='Build ESP project for different targets')
    parser.add_argument('target', choices=['esp32', 'esp32s3'], nargs='?', default='esp32',
                        help='Target chip type (default: esp32)')
    parser.add_argument('--clean', action='store_true',
                        help='Clean build directory before building')
    parser.add_argument('--flash-4mb', dest='flash_4mb', action='store_true',
                        help='build for 4 MB flash modules; the default partition table assumes 8 MB, '
                             'and on a size mismatch the device boot-loops until reflashed over serial')
    args = parser.parse_args()

    base_defaults = Path(f'sdkconfig.defaults.{args.target}')
    if not base_defaults.exists():
        print(f'Error: {base_defaults} not found!')
        return 1

    secret_defaults = Path('sdkconfig.defaults.secret')
    all_defaults = [str(base_defaults.resolve())]
    if secret_defaults.exists():
        all_defaults.append(str(secret_defaults.resolve()))
    if args.flash_4mb:
        all_defaults.append(str(Path('sdkconfig.defaults.4mb').resolve()))  # 4 MB overlay last, so it overrides

    os.environ['IDF_TARGET'] = args.target
    os.environ['SDKCONFIG_DEFAULTS'] = ';'.join(all_defaults)
    print(f'Using target: {args.target}')
    print(f'Using defaults: {all_defaults}')

    if args.clean:
        print('Running full clean...')
        subprocess.run(['idf.py', 'fullclean'], check=False)
        for config in ['sdkconfig', 'sdkconfig.old']:
            if Path(config).exists():
                print(f'Removing {config}')
                Path(config).unlink()
    elif args.flash_4mb:
        # ESP-IDF reads SDKCONFIG_DEFAULTS only when generating sdkconfig from scratch.
        # An existing sdkconfig (e.g. from a prior 8 MB build) is reused verbatim, so the
        # 4 MB overlay would be silently ignored and the build would still SW_RESET-loop on
        # a 4 MB chip. Drop the stale sdkconfig so the overlay is guaranteed to take effect.
        for config in ['sdkconfig', 'sdkconfig.old']:
            if Path(config).exists():
                print(f'--flash-4mb: removing stale {config} so the 4 MB overlay is applied')
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
