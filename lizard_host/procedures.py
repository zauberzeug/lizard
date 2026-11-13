"""Exchanges with a Lizard microcontroller as procedures without I/O.

A procedure is a generator that yields an ``Exchange`` -- a line to send and the line to wait for -- and receives the
matching line or None after the timeout. ``Connection`` and ``AsyncConnection`` run them on a serial port or on the
read loop of an async host, so the steps of e.g. writing a startup script are written down only once.
"""
import json
import logging
import re
from dataclasses import dataclass
from typing import Callable, Dict, Generator, List, Mapping, Optional, TypeVar

from .version import LizardVersion, format_release, parse_info, parse_info_line, required_features

T = TypeVar('T')

log = logging.getLogger('lizard_host')

EXPANDER = re.compile(r'\bExpander\s*\(')
BUS_PAYLOAD_CAPACITY = 256  # must match SerialBus::PAYLOAD_CAPACITY in main/modules/serial_bus.h


@dataclass(frozen=True)
class Exchange:
    """Send a line, if any, then wait up to ``timeout`` seconds for a line that ``expect`` accepts, if given.

    The lines are given without line end and checksum; the connection adds and removes them.
    """
    command: Optional[str] = None
    expect: Optional[Callable[[str], bool]] = None
    timeout: float = 0.0


Procedure = Generator[Exchange, Optional[str], T]


@dataclass(frozen=True)
class Target:
    """A microcontroller: the one on the console, or a peer that serial bus ``bus`` of that one reaches."""
    bus: Optional[str] = None
    peer_id: Optional[int] = None

    @classmethod
    def peer(cls, bus: str, peer_id: int) -> 'Target':
        return cls(bus, peer_id)

    def command(self, text: str) -> str:
        """The line that runs a command on this microcontroller."""
        if self.bus is None:
            return text
        if len(text.encode()) >= BUS_PAYLOAD_CAPACITY:
            raise ValueError(f'Line too long for serial bus {self.bus} ({len(text.encode())} bytes): {text}')
        return f'{self.bus}.send({self.peer_id}, {json.dumps(text.replace("%", "%%"), ensure_ascii=False)})'

    @property
    def prefix(self) -> str:
        """What the console puts in front of a line of this microcontroller."""
        return '' if self.bus is None else f'{self.bus}[{self.peer_id}]: '

    def __str__(self) -> str:
        return 'core' if self.bus is None else f'{self.bus}[{self.peer_id}]'


CORE = Target()


class StartupError(Exception):
    """Writing or verifying a startup script failed."""


class UnsupportedFirmware(Exception):
    """The firmware on a microcontroller is too old for a startup script."""


def send(command: str, target: Target = CORE) -> Procedure[None]:
    """Send a command without waiting for anything."""
    yield Exchange(target.command(command))


def request(command: str, prefix: str, target: Target = CORE, *, timeout: float = 3.0) -> Procedure[Optional[str]]:
    """Send a command and return the first line that starts with ``prefix``, or None after the timeout.

    For a peer the line has the coordinator's ``<bus>[<id>]: `` in front, which is not returned.
    Lines carry no request ID, so an earlier command with the same answer can satisfy the wait.
    """
    full_prefix = target.prefix + prefix
    line = yield Exchange(target.command(command), lambda line: line.startswith(full_prefix), timeout)
    return None if line is None else line[len(target.prefix):]


def read_startup_checksum(target: Target = CORE, *, timeout: float = 3.0) -> Procedure[Optional[int]]:
    """Return the checksum of the startup script the microcontroller holds, or of the edit while there is one."""
    line = yield from request('core.startup_checksum()', 'checksum: ', target, timeout=timeout)
    if line is None:
        return None
    try:
        return int(line.split()[-1], 16)
    except ValueError:
        return None


def read_version(target: Target = CORE, *, timeout: float = 3.0) -> Procedure[Optional[LizardVersion]]:
    """Return the Lizard version the firmware is built from, or None if it is unknown or there is no answer."""
    info: Dict[str, str] = {}

    def expect(line: str) -> bool:
        return line.startswith(target.prefix) and parse_info_line(line[len(target.prefix):]) is not None

    exchange = Exchange(target.command('core.info()'), expect, timeout)
    while True:
        line = yield exchange
        if line is None:
            break
        label, value = parse_info_line(line[len(target.prefix):]) or ('', '')
        info[label] = value
        if label == 'idf version':  # the last line
            break
        exchange = Exchange(None, expect, 1.0)
    return parse_info(info) if info else None


def startup_lines(script: str) -> List[str]:
    """The lines a startup script is stored as."""
    return script.splitlines()


def startup_checksum(script: str) -> int:
    """The checksum ``core.startup_checksum()`` reports for the stored script: the sum of its bytes and line ends."""
    return _lines_checksum(startup_lines(script))


def check_script(script: str, version: Optional[LizardVersion], target: Target = CORE) -> None:
    """Refuse a script that uses a feature the target's firmware lacks, and warn if the version leaves it open."""
    for feature in required_features(script):
        support = 'unsure' if version is None else version.supports(feature.release)
        needed = format_release(feature.release)
        if support == 'no':
            raise UnsupportedFirmware(f'The startup script for {target} uses {feature.name}, which needs Lizard '
                                      f'{needed} or newer, but the firmware is built from Lizard {version}.')
        if support == 'unsure':
            built_from = 'an unknown Lizard version' if version is None else f'Lizard {version}'
            log.warning('The startup script for %s uses %s, which needs Lizard %s or newer; '
                        'the firmware is built from %s and may lack it.', target, feature.name, needed, built_from)


def write_startup(script: str, target: Target = CORE, *,
                  attempts: int = 3, ready_timeout: Optional[float] = None, check_version: bool = True,
                  pace: Optional[int] = None) -> Procedure[None]:
    """Write a startup script, verify it, persist it, restart the microcontroller and verify it again.

    The script is checked with ``core.startup_checksum()`` before it is persisted with ``!.``,
    so a lossy connection cannot leave a corrupt script in the microcontroller's storage.
    After the restart, which has to end with the microcontroller's ``Ready.``, the persisted script is verified once
    more, which also catches a failed ``!.``.
    With ``check_version`` a script that uses a feature the firmware lacks is refused before anything is written.
    Every ``pace`` lines the checksum of the lines so far is checked, which waits until they arrived and finds a lost
    line early; by default every 20 lines for a peer, whose lines pass the coordinator's outbound queue, and never for
    the core.
    The wait for ``Ready.`` takes ``ready_timeout`` seconds, by default 15 s plus 5 s for every ``Expander`` of the
    script, whose constructor waits up to its ``boot_timeout`` of 5 s for the expander to boot.

    :raises StartupError: when the script cannot be written or verified
    :raises UnsupportedFirmware: when the firmware lacks a feature the script uses
    :raises ValueError: when a line of a peer's script does not fit into a bus message
    """
    check_lines(script, target)
    if check_version:
        yield from check_firmware(script, target)
    yield from _write(script, target, attempts=attempts, ready_timeout=ready_timeout, pace=pace)


def write_startups(scripts: Mapping[Target, str], *, attempts: int = 3, ready_timeout: Optional[float] = None,
                   check_version: bool = True) -> Procedure[None]:
    """Write one startup script per microcontroller, the core first, so that its serial buses reach the peers.

    All scripts are checked before the first one is written, so a refused script leaves every microcontroller as it is.
    A peer that does not report its version then, e.g. because the core's script brings the bus that reaches it,
    is checked once the core's script is written.
    """
    targets = sorted(scripts, key=lambda target: target.bus is not None)
    for target in targets:
        check_lines(scripts[target], target)
    unanswered: List[Target] = []
    if check_version:
        for target in targets:
            if required_features(scripts[target]):
                version = yield from read_version(target)
                if version is None and target.bus is not None and CORE in scripts:
                    unanswered.append(target)
                else:
                    check_script(scripts[target], version, target)
    for target in targets:
        if target in unanswered:
            yield from check_firmware(scripts[target], target)
        yield from _write(scripts[target], target, attempts=attempts, ready_timeout=ready_timeout, pace=None)


def check_lines(script: str, target: Target = CORE) -> None:
    """Refuse a script with a line that does not fit into a message to the target, e.g. a bus message to a peer."""
    for line in startup_lines(script):
        target.command(f'!+{line}')


def check_firmware(script: str, target: Target = CORE) -> Procedure[None]:
    """Read the Lizard version of the target if the script needs it, and refuse the script if the firmware lacks a
    feature it uses."""
    if required_features(script):
        version = yield from read_version(target)
        check_script(script, version, target)


def _write(script: str, target: Target, *,
           attempts: int, ready_timeout: Optional[float], pace: Optional[int]) -> Procedure[None]:
    lines = startup_lines(script)
    if ready_timeout is None:
        code = [line for line in lines if not line.lstrip().startswith('#')]
        ready_timeout = 15.0 + 5.0 * sum(bool(EXPANDER.search(line)) for line in code)
    expected = startup_checksum(script)
    if pace is None and target.bus is not None:
        pace = 20
    checksum: Optional[int] = None
    for _ in range(attempts):
        checksum = yield from _send_lines(lines, target, pace)
        if checksum == expected:
            break
    else:
        # restart to drop the unpersisted edit, so core.startup_checksum() reports the stored script again
        yield from send('core.restart()', target)
        raise StartupError(f'Writing the startup script to {target} failed: {_describe(checksum, expected)}')
    yield from send('!.', target)
    ready = yield Exchange(target.command('core.restart()'), lambda line: line == target.prefix + 'Ready.',
                           ready_timeout)
    if ready is None:
        raise StartupError(f'{target} did not report "Ready." within {ready_timeout:.0f} s after its restart')
    checksum = None
    for _ in range(3):
        checksum = yield from read_startup_checksum(target, timeout=1.0)
        if checksum is not None:
            break
    if checksum != expected:
        raise StartupError(f'Verifying the stored startup script of {target} failed: {_describe(checksum, expected)}')


def _send_lines(lines: List[str], target: Target, pace: Optional[int]) -> Procedure[Optional[int]]:
    """Send a script as an edit and return the checksum the target reports for it, or for the lines up to a loss."""
    yield from send('!-', target)
    for count, line in enumerate(lines, 1):
        yield from send(f'!+{line}', target)
        if pace and count % pace == 0 and count < len(lines):
            checksum = yield from read_startup_checksum(target)
            if checksum != _lines_checksum(lines[:count]):
                return checksum  # a line got lost; the next attempt starts over
    return (yield from read_startup_checksum(target))


def _lines_checksum(lines: List[str]) -> int:
    return sum(''.join(f'{line}\n' for line in lines).encode()) % 0x10000


def _describe(checksum: Optional[int], expected: int) -> str:
    if checksum is None:
        return 'no checksum received'
    return f'checksum {checksum:04x} instead of {expected:04x}'
