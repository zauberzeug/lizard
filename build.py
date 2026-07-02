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
    parser.add_argument('--flash-size', dest='flash_size', choices=['4mb', '8mb'], default='8mb',
                        help='flash size variant (default: 8mb). 8mb uses the default partition table; '
                             '4mb builds into an isolated build dir with a 4 MB-fitting table. '
                             'On a size mismatch the device boot-loops until reflashed over serial.')
    args = parser.parse_args()

    base_defaults = Path(f'sdkconfig.defaults.{args.target}')
    if not base_defaults.exists():
        print(f'Error: {base_defaults} not found!')
        return 1

    secret_defaults = Path('sdkconfig.defaults.secret')
    all_defaults = [str(base_defaults.resolve())]
    if secret_defaults.exists():
        all_defaults.append(str(secret_defaults.resolve()))

    # Each flash-size variant gets its OWN generated sdkconfig and build dir, so the two never
    # share mutable state: switching between them can't leak a stale partition table in either
    # direction, and the 8 MB / 4 MB artifacts coexist instead of clobbering each other.
    idf_args = ['idf.py']
    if args.flash_size == '4mb':
        all_defaults.append(str(Path('sdkconfig.defaults.4mb').resolve()))  # 4 MB overlay last, so it overrides
        idf_args += ['-B', 'build-4mb', '-D', 'SDKCONFIG=sdkconfig.4mb']

    os.environ['IDF_TARGET'] = args.target
    os.environ['SDKCONFIG_DEFAULTS'] = ';'.join(all_defaults)
    print(f'Using target: {args.target}')
    print(f'Using flash size: {args.flash_size}')
    print(f'Using defaults: {all_defaults}')

    if args.clean:
        print('Running full clean...')
        subprocess.run([*idf_args, 'fullclean'], check=False)
        generated = 'sdkconfig.4mb' if args.flash_size == '4mb' else 'sdkconfig'
        for config in [generated, f'{generated}.old']:
            if Path(config).exists():
                print(f'Removing {config}')
                Path(config).unlink()

    print('Building...')
    result = subprocess.run([*idf_args, 'build'], check=False)
    if result.returncode != 0:
        print('Build failed!')
        return result.returncode

    print(f'Build completed successfully for {args.target}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
