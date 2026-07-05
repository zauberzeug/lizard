#!/usr/bin/env python3
import argparse
import re
import shlex
import subprocess
import sys
import time
from contextlib import contextmanager
from pathlib import Path, PurePosixPath
from typing import Generator, List, Tuple

try:
    import gpiod
except ImportError:
    GPIOD_VERSION = None
else:
    GPIOD_VERSION = 2 if hasattr(gpiod, 'request_lines') else 1

IS_JETSON = Path('/etc/nv_tegra_release').exists()

PIN_COMMANDS = {'flash', 'erase', 'enable', 'disable', 'reset', 'release_pins'}

# Configuration, populated by main() once the arguments are known. Kept at module
# level so the command functions below can read it without threading it through every
# call; importing this module runs no argument parsing and touches no hardware.
args = argparse.Namespace()
ON = 0
OFF = 1
DRY_RUN = False
CHIP = 'esp32'
DEVICE = ''
EN = 'PR.04'
G0 = 'PAC.06'
FLASH_FREQ = '40m'
BOOTLOADER_OFFSET = '0x1000'
BOOTLOADER = 'build/bootloader/bootloader.bin'
PARTITION_TABLE = 'build/partition_table/partition-table.bin'
FIRMWARE = 'build/lizard.bin'
ELF = 'build/lizard.elf'


class GpioController:
    def __init__(self, en: str, g0: str) -> None:
        pass

    def request_outputs(self, consumer: str) -> None:
        """Request output lines."""

    def set_value(self, key: str, value: int) -> None:
        """Set the value of a line."""

    def release(self) -> None:
        """Release the lines."""


class GpioControllerV1(GpioController):

    def __init__(self, en: str, g0: str) -> None:
        chip = gpiod.Chip('gpiochip0')
        self._lines = {
            'en': chip.find_line(en),
            'g0': chip.find_line(g0),
        }
        if None in self._lines.values():
            raise RuntimeError(f'GPIO lines {en}/{g0} not found on gpiochip0')

    def request_outputs(self, consumer: str) -> None:
        for line in self._lines.values():
            line.request(consumer=consumer, type=gpiod.LINE_REQ_DIR_OUT)

    def set_value(self, key: str, value: int) -> None:
        self._lines[key].set_value(value)

    def release(self) -> None:
        for line in self._lines.values():
            line.release()


class GpioControllerV2(GpioController):
    CHIP_PATH = '/dev/gpiochip0'

    def __init__(self, en: str, g0: str) -> None:
        with gpiod.Chip(self.CHIP_PATH) as chip:
            self._offsets = {
                'en': chip.line_offset_from_id(en),
                'g0': chip.line_offset_from_id(g0),
            }
        self._request = None

    def request_outputs(self, consumer: str) -> None:
        from gpiod.line import Direction  # pylint: disable=import-outside-toplevel
        config = {offset: gpiod.LineSettings(direction=Direction.OUTPUT) for offset in self._offsets.values()}
        self._request = gpiod.request_lines(self.CHIP_PATH, consumer=consumer, config=config)

    def set_value(self, key: str, value: int) -> None:
        from gpiod.line import Value  # pylint: disable=import-outside-toplevel
        if self._request:
            self._request.set_value(self._offsets[key], Value.ACTIVE if value else Value.INACTIVE)

    def release(self) -> None:
        if self._request:
            self._request.release()
            self._request = None


# No-op controller until main() resolves the real one for a local pin command.
gpio: GpioController = GpioController('PR.04', 'PAC.06')


def build_parser() -> argparse.ArgumentParser:
    """Build the espresso argument parser.

    Value flags default to ``None`` so that "explicitly set by the user" can be told
    apart from "left at its default" -- which is what lets ``--host`` forward only the
    flags the user actually typed (see remote_command) and resolve the real defaults on
    the machine that will use them.
    """
    parser = argparse.ArgumentParser(description='Flash and control an ESP32 microcontroller from a Jetson board')
    parser.add_argument('command', choices=['flash', 'enable', 'disable', 'reset', 'erase', 'release_pins', 'coredump'],
                        help='Command to execute')
    parser.add_argument('--host', default=None,
                        help='Run on a remote user@host[:path]: rsync espresso.py + build artifacts there '
                             'and re-invoke the command over SSH (default path: ~/lizard)')
    parser.add_argument('--nand', action='store_true', help='Board has NAND gates')
    parser.add_argument('--bootloader', default=None,
                        help='Path to bootloader (default: build/bootloader/bootloader.bin)')
    parser.add_argument('--partition-table', default=None,
                        help='Path to partition table (default: build/partition_table/partition-table.bin)')
    parser.add_argument('--swap', action='store_true',
                        help='Swap En and G0 pins for piggyboard version lower than v0.5')
    parser.add_argument('--firmware', default=None, help='Path to firmware binary (default: build/lizard.bin)')
    parser.add_argument('--chip', choices=['esp32', 'esp32s3'], default=None, help='ESP chip type (default: esp32)')
    parser.add_argument('--reset-partition', action='store_true', help='Reset to default OTA partition after flashing')
    parser.add_argument('-d', '--dry-run', action='store_true', help='Dry run')
    parser.add_argument('--device', nargs='?', default=None, help='Serial device path (auto-detected on Jetson)')
    parser.add_argument('--elf', default=None, help='coredump: path to ELF file (default: build/lizard.elf)')
    parser.add_argument('--debug', action='store_true',
                        help='coredump: start a debug session, then return to the shell')
    return parser


def resolve_default_device() -> str:
    """Return the default serial device for the machine actually running the command."""
    tegra = Path('/etc/nv_tegra_release')
    if tegra.exists() and (match := re.search(r'R(\d+)', tegra.read_text(encoding='utf-8'))):
        major = int(match.group(1))
        if major == 35:
            return '/dev/ttyTHS0'
        if major == 36:
            return '/dev/ttyTHS1'
        raise RuntimeError(f'Unsupported L4T (Linux for Tegra) version: {major}')
    return '/dev/tty.SLAB_USBtoUART'


def build_gpio(en: str, g0: str) -> GpioController:
    """Return a GPIO controller for the EN/G0 pins, or hard-fail on a Jetson without gpiod.

    On a Jetson the pins MUST be controllable, so a missing/broken gpiod is fatal here
    rather than silently degrading to the no-op controller (which would let the flash
    proceed with no pin control and fail later with a generic error). On a non-Jetson dev
    host we fall back to the no-op controller and rely on esptool's DTR/RTS reset.
    """
    if GPIOD_VERSION is None:
        if IS_JETSON:
            raise RuntimeError('Module gpiod is required to control the EN and G0 pins on a Jetson, '
                               'but it is not installed. Install it (e.g. "pip install gpiod").')
        print_fail('Module gpiod not found; relying on esptool reset only (EN and G0 pins are not controllable).')
        return GpioController(en, g0)
    try:
        return {1: GpioControllerV1, 2: GpioControllerV2}[GPIOD_VERSION](en, g0)
    except Exception as error:  # noqa: BLE001 - any gpiod/hardware error means no controllable pins
        if IS_JETSON:
            raise  # on a Jetson the EN/G0 lines must resolve; surface the real error instead of masking it
        print(f'No controllable EN/G0 pins ({error}); falling back to esptool reset only.')
        return GpioController(en, g0)


def parse_host(host: str) -> Tuple[str, str]:
    """Split a ``user@host[:path]`` string into (ssh_target, remote_path)."""
    if ':' in host:
        target, path = host.split(':', 1)
        return target, path
    return host, '~/lizard'


def quote_remote_path(path: str) -> str:
    """Shell-quote a remote path, but let a leading ``~`` / ``~user`` still expand.

    The tail after the home prefix is quoted, so spaces and shell metacharacters in a
    user-supplied ``--host`` path cannot break out into remote shell syntax.
    """
    match = re.match(r'^(~[\w-]*)(/.*)?$', path)
    if match:
        head, tail = match.group(1), match.group(2) or ''
        return head + ('/' + shlex.quote(tail[1:]) if tail else '')
    return shlex.quote(path)


def remote_command(parsed: argparse.Namespace) -> List[str]:
    """Rebuild the espresso argument list to run on the remote, minus ``--host``.

    Only flags the user explicitly set are forwarded, so device/pin/L4T resolution and
    the path defaults all happen on the remote machine -- a local default (e.g. a Mac's
    serial device) can never leak into the remote invocation. File paths must be relative
    so they resolve against the remote target directory after the rsync.
    """
    command = [parsed.command]
    for flag, enabled in (('--nand', parsed.nand),
                          ('--swap', parsed.swap),
                          ('--reset-partition', parsed.reset_partition),
                          ('--dry-run', parsed.dry_run),
                          ('--debug', parsed.debug)):
        if enabled:
            command.append(flag)
    if parsed.chip is not None:
        command += ['--chip', parsed.chip]
    if parsed.device is not None:
        command += ['--device', parsed.device]
    for flag, value in (('--bootloader', parsed.bootloader),
                        ('--partition-table', parsed.partition_table),
                        ('--firmware', parsed.firmware),
                        ('--elf', parsed.elf)):
        if value is None:
            continue
        # Only build/ (plus espresso.py) is rsynced to the remote, so a custom artifact
        # path must resolve inside build/ or it would silently be missing on the remote.
        # Reject ".." components too, so build/../etc/x cannot escape the copied scope.
        posix = PurePosixPath(value)
        if posix.is_absolute() or '..' in posix.parts or posix.parts[:1] != ('build',):
            raise RuntimeError(f'{flag} {value!r}: under --host, artifact paths must be relative and under '
                               'build/ (only build/ and espresso.py are copied to the remote).')
        command += [flag, value]
    return command


def run_remote(host: str, command: List[str], use_sudo: bool) -> None:
    """rsync espresso.py + the build artifacts to the host and run the command over SSH."""
    target, path = parse_host(host)
    quoted_path = quote_remote_path(path)
    print_bold(f'Copying espresso.py and build artifacts to {target}:{path}...')
    subprocess.run(['rsync', '-zarv', '--prune-empty-dirs',
                    '--include=*/', '--include=*.bin', '--include=*.elf', '--exclude=*',
                    'build', f'{target}:{quoted_path}'], check=True)
    subprocess.run(['rsync', '-z', 'espresso.py', f'{target}:{quoted_path}/'], check=True)

    print_bold(f'Running "espresso.py {" ".join(command)}" on {target}...')
    sudo = 'sudo ' if use_sudo else ''
    remote = f'cd {quoted_path} && {sudo}./espresso.py ' + ' '.join(shlex.quote(token) for token in command)
    subprocess.run(['ssh', '-t', target, f'bash --login -c {shlex.quote(remote)}'], check=True)


@contextmanager
def _pin_config() -> Generator[None, None, None]:
    """Configure the EN and G0 pins to control the microcontroller."""
    print_bold('Configuring EN and G0 pins...')
    gpio.request_outputs(consumer='espresso')
    time.sleep(0.5)
    yield
    _release_pins()


def _release_pins() -> None:
    """Release pins."""
    gpio.release()


@contextmanager
def _flash_mode() -> Generator[None, None, None]:
    """Bring the microcontroller into flash mode."""
    print_bold('Bringing the microcontroller into flash mode...')
    set_en(ON)
    set_g0(ON)
    set_en(OFF)
    yield
    _reset()


def enable() -> None:
    """Enable the microcontroller."""
    print_bold('Enabling the microcontroller...')
    with _pin_config():
        set_g0(OFF)
        set_en(OFF)


def disable() -> None:
    """Disable the microcontroller."""
    print_bold('Disabling the microcontroller...')
    with _pin_config():
        set_en(ON)


def reset() -> None:
    """Reset the microcontroller."""
    print_bold('Resetting the microcontroller...')
    with _pin_config():
        _reset()


def _reset() -> None:
    """Set pins to reset the microcontroller."""
    set_g0(OFF)
    set_en(ON)
    set_en(OFF)


def erase() -> None:
    """Erase the microcontroller."""
    print_bold('Erasing the microcontroller...')
    with _pin_config():
        with _flash_mode():
            success = run(
                'esptool.py',
                '--chip', CHIP,
                '--port', DEVICE,
                '--baud', '921600',
                '--before', 'default_reset',
                '--after', 'hard_reset',
                'erase_flash',
            )
            if not success:
                raise RuntimeError('Failed to erase flash.')


def reset_partition() -> None:
    """Reset the OTA partition to the default state."""
    print_bold('Resetting partition to "ota_0"...')
    success = run(
        'esptool.py',
        '--chip', CHIP,
        '--port', DEVICE,
        '--baud', '115200',
        'erase_region',
        '0xf000',  # otadata partition offset (default ESP-IDF partition table)
        '0x2000',  # otadata partition size (default ESP-IDF partition table)
    )
    if not success:
        raise RuntimeError('Failed to reset OTA partition.')


def flash() -> None:
    """Flash the microcontroller."""
    print_bold('Flashing...')
    with _pin_config():
        with _flash_mode():
            success = run(
                'esptool.py',
                '--chip', CHIP,
                '--port', DEVICE,
                '--baud', '921600',
                '--before', 'default_reset',
                '--after', 'hard_reset',
                'write_flash',
                '-z',
                '--flash_mode', 'dio',
                '--flash_freq', FLASH_FREQ,
                '--flash_size', 'detect',
                BOOTLOADER_OFFSET, BOOTLOADER,
                '0x8000', PARTITION_TABLE,
                '0x20000', FIRMWARE,
            )
            if not success:
                raise RuntimeError('Flashing failed. Use "sudo" and check your parameters.')
            if args.reset_partition:
                reset_partition()


def coredump() -> None:
    """Read (or debug) an ESP32 core dump over the serial device.

    Wraps esp_coredump, reusing espresso's serial-device selection and --chip. Import is
    deferred so the flash path does not depend on esp_coredump being installed.
    """
    print_bold('Reading core dump...')
    print(f'  port={DEVICE} chip={CHIP} elf={ELF}')
    if DRY_RUN:
        return
    from esp_coredump import CoreDump  # pylint: disable=import-outside-toplevel
    dump = CoreDump(chip=CHIP, port=DEVICE, baud=115200, prog=ELF)
    if args.debug:
        dump.dbg_corefile()
    else:
        dump.info_corefile()


def run(*run_args: str) -> bool:
    """Run a command and return whether it was successful."""
    print(f'  {" ".join(run_args)}')
    if DRY_RUN:
        return True
    result = subprocess.run(run_args, check=False)
    return result.returncode == 0


def set_en(value: int) -> None:
    print(f'  Setting EN pin to {value}')
    if not DRY_RUN:
        gpio.set_value('en', value)
        time.sleep(0.5)


def set_g0(value: int) -> None:
    print(f'  Setting G0 pin to {value}')
    if not DRY_RUN:
        gpio.set_value('g0', value)
        time.sleep(0.5)


def print_ok(message: str) -> None:
    print_bold(f'\033[94m{message}\033[0m')


def print_bold(message: str) -> None:
    print(f'\033[1m{message}\033[0m')


def print_fail(message: str) -> None:
    print(f'\033[91m{message}\033[0m')


def main(argv: List[str]) -> None:
    global args, ON, OFF, DRY_RUN, CHIP, DEVICE, EN, G0
    global FLASH_FREQ, BOOTLOADER_OFFSET, BOOTLOADER, PARTITION_TABLE, FIRMWARE, ELF, gpio

    args = build_parser().parse_args(argv)

    if args.host is not None:
        # Validate the arguments locally with the real parser (done above), then hand the
        # command to the remote machine, which resolves its own device/pins/L4T.
        run_remote(args.host, remote_command(args), use_sudo=not args.dry_run)
        return

    print_ok('Espresso dry-running...' if args.dry_run else 'Espresso running...')

    ON = 1 if args.nand else 0
    OFF = 0 if args.nand else 1
    DRY_RUN = args.dry_run
    CHIP = args.chip or 'esp32'
    DEVICE = args.device if args.device is not None else resolve_default_device()
    FLASH_FREQ = {'esp32': '40m', 'esp32s3': '80m'}[CHIP]
    BOOTLOADER_OFFSET = {'esp32': '0x1000', 'esp32s3': '0x0'}[CHIP]
    BOOTLOADER = args.bootloader or 'build/bootloader/bootloader.bin'
    PARTITION_TABLE = args.partition_table or 'build/partition_table/partition-table.bin'
    FIRMWARE = args.firmware or 'build/lizard.bin'
    ELF = args.elf or 'build/lizard.elf'

    EN, G0 = 'PR.04', 'PAC.06'
    if args.swap:
        EN, G0 = G0, EN
    if args.command in PIN_COMMANDS:
        gpio = build_gpio(EN, G0)

    if args.command == 'enable':
        enable()
    elif args.command == 'disable':
        disable()
    elif args.command == 'reset':
        reset()
    elif args.command == 'erase':
        erase()
    elif args.command == 'flash':
        flash()
    elif args.command == 'release_pins':
        _release_pins()
    elif args.command == 'coredump':
        coredump()
    else:
        raise RuntimeError(f'Invalid command "{args.command}".')
    print_ok('Finished. ☕️')


if __name__ == '__main__':
    try:
        main(sys.argv[1:])
    except (RuntimeError, subprocess.CalledProcessError) as e:
        print_fail(f'Error: {e}')
        sys.exit(1)
