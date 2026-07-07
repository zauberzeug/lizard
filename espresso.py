#!/usr/bin/env python3
import argparse
import re
import shlex
import subprocess
import sys
import time
from contextlib import contextmanager
from dataclasses import dataclass, field, replace
from pathlib import Path, PurePosixPath
from typing import Generator, List, Optional, Tuple

try:
    import gpiod
except ImportError:
    GPIOD_VERSION = None
else:
    GPIOD_VERSION = 2 if hasattr(gpiod, 'request_lines') else 1

IS_JETSON = Path('/etc/nv_tegra_release').exists()

PIN_COMMANDS = {'flash', 'erase', 'enable', 'disable', 'reset', 'release_pins'}

# Artifact dests and the extension each must carry under --host. Only build/ is rsynced, and
# only the command's include pattern (flash→*.bin, coredump→*.elf) is transferred, so a custom
# path must be under build/ AND match that extension or it would be silently absent on the remote.
ARTIFACT_SUFFIX = {'bootloader': '.bin', 'partition_table': '.bin', 'firmware': '.bin', 'elf': '.elf'}

# Which build/ artifacts each remote command needs rsynced (others need none: erase/enable/
# disable/reset/release_pins control pins only). Commands absent here skip the artifact rsync.
REMOTE_ARTIFACT_INCLUDES = {
    'flash': ['--include=*/', '--include=*.bin'],
    'coredump': ['--include=*/', '--include=*.elf'],
}


class GpioController:
    """No-op fallback controller; also the base class for the real gpiod-backed ones."""

    def __init__(self, en: str = '', g0: str = '') -> None:
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


@dataclass(frozen=True)
class Config:
    """Resolved runtime configuration, passed to the command functions.

    The field defaults are the single source of the option defaults: build_parser
    interpolates them into the help texts via the DEFAULT instance and main() applies
    them as fallbacks (the parser itself keeps None defaults so that main() can tell
    typed flags from omitted ones). Values that depend on other fields are properties,
    so they cannot drift from their inputs.
    """
    chip: str = 'esp32'
    device: str = ''  # the real default is machine-dependent, see resolve_default_device()
    baud: Optional[int] = None  # an explicit --baud; each command falls back to its own default
    nand: bool = False
    swap: bool = False
    dry_run: bool = False
    no_stub: bool = False
    reset_partition: bool = False
    debug: bool = False
    bootloader: str = 'build/bootloader/bootloader.bin'
    partition_table: str = 'build/partition_table/partition-table.bin'
    firmware: str = 'build/lizard.bin'
    elf: str = 'build/lizard.elf'
    en_pin: str = 'PR.04'
    g0_pin: str = 'PAC.06'
    gpio: GpioController = field(default_factory=GpioController)  # no-op until main() builds the real one

    @property
    def on(self) -> int:
        return 1 if self.nand else 0

    @property
    def off(self) -> int:
        return 0 if self.nand else 1

    @property
    def en(self) -> str:
        return self.g0_pin if self.swap else self.en_pin

    @property
    def g0(self) -> str:
        return self.en_pin if self.swap else self.g0_pin

    @property
    def stub_args(self) -> Tuple[str, ...]:
        return ('--no-stub',) if self.no_stub else ()

    @property
    def flash_freq(self) -> str:
        return {'esp32': '40m', 'esp32s3': '80m'}[self.chip]

    @property
    def bootloader_offset(self) -> str:
        return {'esp32': '0x1000', 'esp32s3': '0x0'}[self.chip]

    @property
    def flash_baud(self) -> str:
        return str(self.baud or 921600)

    @property
    def coredump_baud(self) -> int:
        return self.baud or 115200  # esp_coredump's own default; the flash/erase baud would be wrong here


DEFAULT = Config()


def build_parser() -> argparse.ArgumentParser:
    """Build the espresso argument parser.

    Value flags default to ``None``; the real defaults are resolved in main() on the
    machine that ultimately runs the command (under ``--host`` that is the remote).
    Abbreviated flags are disabled so that every token has exactly one spelling and the
    argv pass-through in remote_command cannot misread e.g. ``--ho`` for ``--host``.
    """
    parser = argparse.ArgumentParser(description='Flash and control an ESP32 microcontroller from a Jetson board',
                                     allow_abbrev=False)
    parser.add_argument('command', choices=['flash', 'enable', 'disable', 'reset', 'erase', 'release_pins', 'coredump'],
                        help='Command to execute')
    parser.add_argument('--host', default=None,
                        help='Run on a remote user@host[:path]: rsync espresso.py + build artifacts there '
                             'and re-invoke the command over SSH (default path: ~/lizard)')
    parser.add_argument('--nand', action='store_true', help='Board has NAND gates')
    parser.add_argument('--bootloader', default=None,
                        help=f'Path to bootloader (default: {DEFAULT.bootloader})')
    parser.add_argument('--partition-table', default=None,
                        help=f'Path to partition table (default: {DEFAULT.partition_table})')
    parser.add_argument('--swap', action='store_true',
                        help='Swap En and G0 pins for piggyboard version lower than v0.5')
    parser.add_argument('--firmware', default=None, help=f'Path to firmware binary (default: {DEFAULT.firmware})')
    parser.add_argument('--chip', choices=['esp32', 'esp32s3'], default=None,
                        help=f'ESP chip type (default: {DEFAULT.chip})')
    parser.add_argument('--reset-partition', action='store_true', help='Reset to default OTA partition after flashing')
    parser.add_argument('-d', '--dry-run', action='store_true', help='Dry run')
    parser.add_argument('--device', nargs='?', default=None, help='Serial device path (auto-detected on Jetson)')
    parser.add_argument('--baud', type=int, default=None,
                        help=f'Baud rate (default: {DEFAULT.flash_baud} for flashing and erasing, '
                             f'{DEFAULT.coredump_baud} for coredump)')
    parser.add_argument('--no-stub', action='store_true',
                        help="Do not upload esptool's RAM stub (slower, but avoids stub upload failures "
                             'on some USB-UART bridges)')
    parser.add_argument('--elf', default=None, help=f'coredump: path to ELF file (default: {DEFAULT.elf})')
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
    if sys.platform.startswith('linux'):
        return '/dev/ttyUSB0'  # a USB-UART bridge on a non-Jetson Linux host
    return '/dev/tty.SLAB_USBtoUART'  # the same CP210x bridge under macOS


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
    """Split a ``user@host[:path]`` string into (ssh_target, remote_path).

    A trailing colon means the remote home directory, as in scp/rsync -- mapped to ``~``
    explicitly, because a literal empty path would make rsync/ssh mutate the wrong
    remote location. Without a colon the default ``~/lizard`` is used.
    """
    if ':' not in host:
        return host, '~/lizard'
    target, _, path = host.partition(':')
    return target, path or '~'


def quote_remote_path(path: str) -> str:
    """Quote a remote path for the ssh command line, letting a leading ``~`` / ``~user`` expand.

    The tail after the home prefix is quoted, so spaces and shell metacharacters in a
    user-supplied ``--host`` path cannot break out into remote shell syntax. Only the ssh
    command needs this: rsync destinations get the raw path, because rsync (>= 3.2.4)
    escapes remote args itself and quoting would end up literal in the directory name.
    """
    match = re.match(r'^(~[\w-]*)(/.*)?$', path)
    if match:
        head, tail = match.group(1), match.group(2) or ''
        return head + ('/' + shlex.quote(tail[1:]) if tail else '')
    return shlex.quote(path)


def _require_build_relative(flag: str, value: str, suffix: str) -> None:
    """Reject a custom artifact path that would fall outside the rsynced build/ scope.

    Only build/ (plus espresso.py) is copied to the remote, and only files matching the
    command's include pattern are transferred, so a custom artifact path must resolve inside
    build/ AND carry the expected extension -- otherwise it is accepted here but silently
    missing on the remote. ".." components are rejected too, so build/../etc/x cannot escape.
    """
    posix = PurePosixPath(value)
    if posix.is_absolute() or '..' in posix.parts or posix.parts[:1] != ('build',):
        raise RuntimeError(f'{flag} {value!r}: under --host, artifact paths must be relative and under '
                           'build/ (only build/ and espresso.py are copied to the remote).')
    if posix.suffix != suffix:
        raise RuntimeError(f'{flag} {value!r}: under --host, this must be a {suffix} file under build/ '
                           f'(only build/**/*{suffix} is rsynced to the remote for this command).')


def remote_command(argv: List[str], parsed: argparse.Namespace) -> List[str]:
    """Return the argument list to run on the remote: the local argv minus ``--host``/``--dry-run``.

    The user's tokens pass through verbatim instead of being reconstructed from parsed
    values, so nothing the user typed can be dropped or reshaped and no locally resolved
    default can leak into the remote invocation. Only the two orchestration flags are
    stripped: ``--host`` (it IS the remote dispatch) and ``--dry-run`` (a dry run prints
    the rsync/ssh commands locally and must not run anything remotely, see run_remote).
    """
    for dest, suffix in ARTIFACT_SUFFIX.items():
        value = getattr(parsed, dest)
        if value is not None:
            _require_build_relative('--' + dest.replace('_', '-'), str(value), suffix)
    command = []
    skip_value = False
    for token in argv:
        if skip_value:
            skip_value = False
        elif token == '--host':
            skip_value = True
        elif token.startswith('--host=') or token in ('-d', '--dry-run'):
            pass
        else:
            command.append(token)
    return command


def run_remote(host: str, command: List[str], *, artifact_includes: List[str],
               use_sudo: bool, dry_run: bool) -> None:
    """rsync espresso.py (+ the needed build artifacts) to the host and run the command over SSH.

    Only the artifacts the command actually uses are copied: ``artifact_includes`` is empty for
    pin-only commands (erase/enable/disable/reset/release_pins), so they no longer die in an
    rsync of a missing build/ on a fresh clone. build/ resolves against the cwd -- the same base
    a local run uses for the artifact paths -- so the remote flashes exactly what a local flash
    would; only espresso.py itself is taken from the script's own directory. Under ``dry_run``
    nothing is sent or run on the remote -- the rsync/ssh commands are printed so a dry run
    never mutates the remote.
    """
    target, path = parse_host(host)
    if target.startswith('-'):
        raise RuntimeError(f'Refusing host {target!r}: a leading dash could be parsed as an ssh/rsync option.')
    script_dir = Path(__file__).resolve().parent
    runner = _print_remote if dry_run else _run_checked

    if artifact_includes:
        build_dir = Path('build')
        if not dry_run and not build_dir.is_dir():
            raise RuntimeError(f'No build/ directory in {Path.cwd()}; run the ESP-IDF build first.')
        print_bold(f'Copying build artifacts to {target}:{path}...')
        runner(['rsync', '-zarv', '--prune-empty-dirs', *artifact_includes, '--exclude=*',
                str(build_dir), f'{target}:{path}'])
    print_bold(f'Copying espresso.py to {target}:{path}...')
    runner(['rsync', '-z', str(script_dir / 'espresso.py'), f'{target}:{path}/'])

    print_bold(f'Running "espresso.py {" ".join(command)}" on {target}...')
    sudo = 'sudo ' if use_sudo else ''
    remote = f'cd {quote_remote_path(path)} && {sudo}./espresso.py ' + ' '.join(shlex.quote(token) for token in command)
    runner(['ssh', '-t', target, f'bash --login -c {shlex.quote(remote)}'])


def _run_checked(command: List[str]) -> None:
    subprocess.run(command, check=True)


def _print_remote(command: List[str]) -> None:
    print(f'  {" ".join(shlex.quote(token) for token in command)}')


@contextmanager
def _pin_config(config: Config) -> Generator[None, None, None]:
    """Configure the EN and G0 pins to control the microcontroller."""
    print_bold('Configuring EN and G0 pins...')
    config.gpio.request_outputs(consumer='espresso')
    time.sleep(0.5)
    yield
    _release_pins(config)


def _release_pins(config: Config) -> None:
    """Release pins."""
    config.gpio.release()


@contextmanager
def _flash_mode(config: Config) -> Generator[None, None, None]:
    """Bring the microcontroller into flash mode."""
    print_bold('Bringing the microcontroller into flash mode...')
    set_en(config, config.on)
    set_g0(config, config.on)
    set_en(config, config.off)
    yield
    _reset(config)


def enable(config: Config) -> None:
    """Enable the microcontroller."""
    print_bold('Enabling the microcontroller...')
    with _pin_config(config):
        set_g0(config, config.off)
        set_en(config, config.off)


def disable(config: Config) -> None:
    """Disable the microcontroller."""
    print_bold('Disabling the microcontroller...')
    with _pin_config(config):
        set_en(config, config.on)


def reset(config: Config) -> None:
    """Reset the microcontroller."""
    print_bold('Resetting the microcontroller...')
    with _pin_config(config):
        _reset(config)


def _reset(config: Config) -> None:
    """Set pins to reset the microcontroller."""
    set_g0(config, config.off)
    set_en(config, config.on)
    set_en(config, config.off)


def erase(config: Config) -> None:
    """Erase the microcontroller."""
    print_bold('Erasing the microcontroller...')
    with _pin_config(config):
        with _flash_mode(config):
            success = run(
                config,
                'esptool.py',
                '--chip', config.chip,
                '--port', config.device,
                '--baud', config.flash_baud,
                *config.stub_args,
                '--before', 'default_reset',
                '--after', 'hard_reset',
                'erase_flash',
            )
            if not success:
                raise RuntimeError('Failed to erase flash.')


def reset_partition(config: Config) -> None:
    """Reset the OTA partition to the default state."""
    print_bold('Resetting partition to "ota_0"...')
    success = run(
        config,
        'esptool.py',
        '--chip', config.chip,
        '--port', config.device,
        '--baud', config.flash_baud,
        *config.stub_args,
        'erase_region',
        '0xf000',  # otadata partition offset (default ESP-IDF partition table)
        '0x2000',  # otadata partition size (default ESP-IDF partition table)
    )
    if not success:
        raise RuntimeError('Failed to reset OTA partition.')


def flash(config: Config) -> None:
    """Flash the microcontroller."""
    print_bold('Flashing...')
    with _pin_config(config):
        with _flash_mode(config):
            success = run(
                config,
                'esptool.py',
                '--chip', config.chip,
                '--port', config.device,
                '--baud', config.flash_baud,
                *config.stub_args,
                '--before', 'default_reset',
                '--after', 'hard_reset',
                'write_flash',
                '-z',
                '--flash_mode', 'dio',
                '--flash_freq', config.flash_freq,
                '--flash_size', 'detect',
                config.bootloader_offset, config.bootloader,
                '0x8000', config.partition_table,
                '0x20000', config.firmware,
            )
            if not success:
                raise RuntimeError('Flashing failed. Use "sudo" and check your parameters.')
            if config.reset_partition:
                reset_partition(config)


def coredump(config: Config) -> None:
    """Read (or debug) an ESP32 core dump over the serial device.

    Wraps esp_coredump, reusing espresso's serial-device selection and --chip. Import is
    deferred so the flash path does not depend on esp_coredump being installed.
    """
    print_bold('Reading core dump...')
    print(f'  port={config.device} chip={config.chip} baud={config.coredump_baud} elf={config.elf}')
    if config.dry_run:
        return
    try:
        from esp_coredump import CoreDump  # pylint: disable=import-outside-toplevel
    except ImportError as error:
        raise RuntimeError('Module esp_coredump is required for the coredump command, but it is not installed. '
                           'Install it on the machine reading the dump (e.g. "pip install esp-coredump").') from error
    dump = CoreDump(chip=config.chip, port=config.device, baud=config.coredump_baud, prog=config.elf)
    if config.debug:
        dump.dbg_corefile()
    else:
        dump.info_corefile()


def run(config: Config, *run_args: str) -> bool:
    """Run a command and return whether it was successful."""
    print(f'  {" ".join(run_args)}')
    if config.dry_run:
        return True
    result = subprocess.run(run_args, check=False)
    return result.returncode == 0


def set_en(config: Config, value: int) -> None:
    print(f'  Setting EN pin to {value}')
    if not config.dry_run:
        config.gpio.set_value('en', value)
        time.sleep(0.5)


def set_g0(config: Config, value: int) -> None:
    print(f'  Setting G0 pin to {value}')
    if not config.dry_run:
        config.gpio.set_value('g0', value)
        time.sleep(0.5)


def print_ok(message: str) -> None:
    print_bold(f'\033[94m{message}\033[0m')


def print_bold(message: str) -> None:
    print(f'\033[1m{message}\033[0m')


def print_fail(message: str) -> None:
    print(f'\033[91m{message}\033[0m')


def main(argv: List[str]) -> None:
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.host is not None:
        # Validate the arguments locally with the real parser (done above), then hand the
        # command to the remote machine, which resolves its own device/pins/L4T. sudo is only
        # for the pin/flash commands; coredump wraps esp_coredump (pip --user, dialout not root).
        run_remote(args.host, remote_command(argv, args),
                   artifact_includes=REMOTE_ARTIFACT_INCLUDES.get(args.command, []),
                   use_sudo=args.command in PIN_COMMANDS,
                   dry_run=args.dry_run)
        return

    print_ok('Espresso dry-running...' if args.dry_run else 'Espresso running...')

    config = Config(
        chip=args.chip or DEFAULT.chip,
        device=args.device or resolve_default_device(),
        baud=args.baud,
        nand=args.nand,
        swap=args.swap,
        dry_run=args.dry_run,
        no_stub=args.no_stub,
        reset_partition=args.reset_partition,
        debug=args.debug,
        bootloader=args.bootloader or DEFAULT.bootloader,
        partition_table=args.partition_table or DEFAULT.partition_table,
        firmware=args.firmware or DEFAULT.firmware,
        elf=args.elf or DEFAULT.elf,
    )
    if args.command in PIN_COMMANDS and not args.dry_run:
        config = replace(config, gpio=build_gpio(config.en, config.g0))

    if args.command == 'enable':
        enable(config)
    elif args.command == 'disable':
        disable(config)
    elif args.command == 'reset':
        reset(config)
    elif args.command == 'erase':
        erase(config)
    elif args.command == 'flash':
        flash(config)
    elif args.command == 'release_pins':
        _release_pins(config)
    elif args.command == 'coredump':
        coredump(config)
    else:
        raise RuntimeError(f'Invalid command "{args.command}".')
    print_ok('Finished. ☕️')


if __name__ == '__main__':
    try:
        main(sys.argv[1:])
    except (RuntimeError, subprocess.CalledProcessError) as e:
        print_fail(f'Error: {e}')
        sys.exit(1)
