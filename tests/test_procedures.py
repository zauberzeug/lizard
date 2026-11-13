import asyncio
import json
import logging
import re
from typing import Dict, List, Optional

import pytest

import lizard_host as lh
from lizard_host import procedures
from lizard_host.procedures import Procedure, T

BUS_SEND = re.compile(r'(\w+)\.send\((\d+), (".*")\)')


class FakeLizard:
    """Answers the commands of the procedures like a microcontroller, with peers behind serial bus "bus"."""

    def __init__(self, info: Optional[Dict[str, str]] = None, peers: Optional[Dict[int, 'FakeLizard']] = None) -> None:
        self.info = info or {'project name': 'lizard', 'project version': 'v0.16.0'}
        self.peers = peers or {}
        self.stored = ''
        self.edit: Optional[List[str]] = None
        self.drop_next_line = False  # a "!+" line that gets lost on the way
        self.silent = False
        self.boot_lines = ['Ready.']  # what a restart prints

    def handle(self, command: str) -> List[str]:
        match = BUS_SEND.fullmatch(command)
        if match:
            peer = self.peers[int(match.group(2))]
            text = json.loads(match.group(3)).replace('%%', '%')
            return [f'{match.group(1)}[{match.group(2)}]: {line}' for line in peer.handle(text)]
        if self.silent:
            return []
        if command == '!-':
            self.edit = []
        elif command.startswith('!+'):
            if self.drop_next_line:
                self.drop_next_line = False
            else:
                assert self.edit is not None
                self.edit.append(command[2:])
        elif command == '!.':
            assert self.edit is not None
            self.stored = ''.join(f'{line}\n' for line in self.edit)
            self.edit = None
        elif command == 'core.restart()':
            self.edit = None
            return list(self.boot_lines)
        elif command == 'core.startup_checksum()':
            script = self.stored if self.edit is None else ''.join(f'{line}\n' for line in self.edit)
            return [f'checksum: {sum(script.encode()) % 0x10000:04x}']
        elif command == 'core.info()':
            return [f'{label}: {value}' for label, value in self.info.items()] + ['idf version: v5.3.6']
        return []


def run(procedure: Procedure[T], lizard: FakeLizard, sent: Optional[List[str]] = None) -> T:
    """Run a procedure against a fake microcontroller that answers right away."""
    pending: List[str] = []
    try:
        exchange = next(procedure)
        while True:
            if exchange.command is not None:
                if sent is not None:
                    sent.append(exchange.command)
                pending = lizard.handle(exchange.command)
            answer = None
            while exchange.expect is not None and pending:
                line = pending.pop(0)
                if exchange.expect(line):
                    answer = line
                    break
            exchange = procedure.send(answer)
    except StopIteration as stop:
        return stop.value


SCRIPT = 'led = Output(15)\nled.on()\n'


def test_write_startup():
    lizard = FakeLizard()
    run(procedures.write_startup(SCRIPT), lizard)
    assert lizard.stored == SCRIPT
    assert lh.startup_checksum(SCRIPT) == sum(SCRIPT.encode())


def test_write_startup_retries_a_lost_line():
    lizard = FakeLizard()
    lizard.drop_next_line = True
    sent: List[str] = []
    run(procedures.write_startup(SCRIPT), lizard, sent)
    assert lizard.stored == SCRIPT
    assert sent.count('!-') == 2


def test_write_startup_fails_without_answer():
    lizard = FakeLizard()
    lizard.silent = True
    with pytest.raises(lh.StartupError, match='no checksum received'):
        run(procedures.write_startup(SCRIPT), lizard)


def test_write_startup_to_peer():
    peer = FakeLizard()
    core = FakeLizard(peers={2: peer})
    sent: List[str] = []
    run(procedures.write_startup('print("100%")\n', lh.Target.peer('bus', 2)), core, sent)
    assert peer.stored == 'print("100%")\n'
    assert core.stored == ''
    assert sent[0] == 'bus.send(2, "!-")'
    assert sent[1] == 'bus.send(2, "!+print(\\"100%%\\")")'


def test_write_startups_core_first():
    peer = FakeLizard()
    core = FakeLizard(peers={1: peer})
    sent: List[str] = []
    run(procedures.write_startups({lh.Target.peer('bus', 1): 'a = 1\n', lh.CORE: 'b = 2\n'}), core, sent)
    assert (core.stored, peer.stored) == ('b = 2\n', 'a = 1\n')
    assert sent[0] == '!-'


def test_write_startup_waits_for_its_own_ready():
    lizard = FakeLizard()
    lizard.boot_lines = ['p0: Ready.', 'mybus[1]: Ready.']  # an expander's echo, a peer of another bus
    with pytest.raises(lh.StartupError, match='did not report "Ready."'):
        run(procedures.write_startup(SCRIPT), lizard)
    lizard.boot_lines = ['p0: Ready.', 'Ready.']
    run(procedures.write_startup(SCRIPT), lizard)
    assert lizard.stored == SCRIPT


def test_peer_write_is_paced():
    peer = FakeLizard()
    core = FakeLizard(peers={1: peer})
    script = ''.join(f'float v{i} = {i}\n' for i in range(45))
    peer.drop_next_line = True  # the first line of the first attempt
    sent: List[str] = []
    run(procedures.write_startup(script, lh.Target.peer('bus', 1)), core, sent)
    assert peer.stored == script
    first_attempt = sent[:sent.index('bus.send(1, "!-")', 1)]
    assert len([line for line in first_attempt if '!+' in line]) == 20  # stopped at the first check
    assert len([line for line in sent if 'core.startup_checksum()' in line]) == 1 + 2 + 1 + 1  # checks, final, verify


def test_write_startups_checks_all_scripts_first():
    peer = FakeLizard({'project name': 'lizard', 'version': 'v0.15.1'})
    core = FakeLizard(peers={1: peer})
    with pytest.raises(ValueError, match='too long'):
        run(procedures.write_startups({lh.CORE: SCRIPT, lh.Target.peer('bus', 1): 'x' * 300}), core)
    with pytest.raises(lh.UnsupportedFirmware):
        run(procedures.write_startups({lh.CORE: SCRIPT, lh.Target.peer('bus', 1): 'core.telemetry(a, 100)\n'}), core)
    assert core.stored == peer.stored == ''


def test_connection_needs_read_timeout():
    class Port:
        timeout = None
    with pytest.raises(ValueError, match='read timeout'):
        lh.Connection(Port())


def test_line_too_long_for_bus():
    core = FakeLizard(peers={1: FakeLizard()})
    with pytest.raises(ValueError, match='too long'):
        run(procedures.write_startup('x' * 300, lh.Target.peer('bus', 1)), core)


def test_refuses_old_firmware():
    lizard = FakeLizard({'project name': 'lizard', 'version': 'v0.15.1'})
    with pytest.raises(lh.UnsupportedFirmware, match='0.16.0'):
        run(procedures.write_startup('core.telemetry(1, motor.position)\n'), lizard)
    assert lizard.stored == ''
    run(procedures.write_startup(SCRIPT), lizard)  # no feature, no check
    assert lizard.stored == SCRIPT


def test_warns_between_releases(caplog: pytest.LogCaptureFixture):
    lizard = FakeLizard({'project name': 'my-robot', 'project version': 'v0.2.0',
                         'lizard version': 'v0.15.1-40-gea96593'})
    with caplog.at_level(logging.WARNING, 'lizard_host'):
        run(procedures.write_startup('core.telemetry(1, motor.position)\n'), lizard)
    assert 'may lack it' in caplog.text
    assert lizard.stored


def test_read_version_of_peer():
    peer = FakeLizard({'project name': 'lizard', 'project version': 'v0.16.0'})
    core = FakeLizard(peers={3: peer})
    version = run(procedures.read_version(lh.Target.peer('bus', 3)), core)
    assert version == lh.LizardVersion.parse('v0.16.0')


def test_request_strips_peer_prefix():
    core = FakeLizard(peers={1: FakeLizard()})
    assert run(procedures.request('core.startup_checksum()', 'checksum:', lh.Target.peer('bus', 1)), core) == \
        'checksum: 0000'


class FakePort:
    """A serial port in front of a FakeLizard, with the line format of the console."""

    def __init__(self, lizard: FakeLizard) -> None:
        self.lizard = lizard
        self.incoming = bytearray()

    @property
    def in_waiting(self) -> int:
        return len(self.incoming)

    def write(self, data: bytes) -> None:
        text, check = lh.split_checksum(data.decode())
        assert check == lh.checksum(text)
        for line in self.lizard.handle(text):
            self.incoming.extend(lh.format_line(line).replace('\n', '\r\n').encode())

    def read(self, size: int) -> bytes:
        data = bytes(self.incoming[:size])
        del self.incoming[:size]
        return data


def test_connection():
    lizard = FakeLizard()
    port = FakePort(lizard)
    lines: List[str] = []
    connection = lh.Connection(port, on_line=lines.append)
    port.incoming.extend(b'checksum: 0001\r\ngarbage@00\r\n')  # a stale answer and a damaged line
    assert connection.read_startup_checksum(timeout=0.1) == 0
    connection.write_startup(SCRIPT)
    assert lizard.stored == SCRIPT
    assert connection.read_version() == lh.LizardVersion.parse('v0.16.0')
    assert 'garbage' not in lines
    assert 'Ready.' in lines


def test_async_connection():
    lizard = FakeLizard()

    async def main() -> None:
        connection: lh.AsyncConnection

        async def write(line: str) -> None:
            text, check = lh.split_checksum(line)
            assert check == lh.checksum(text)
            for answer in lizard.handle(text):
                asyncio.get_running_loop().call_soon(connection.feed, answer)  # like a read loop

        connection = lh.AsyncConnection(write)
        await connection.write_startup(SCRIPT)
        assert await connection.request('core.info()', 'project version:') == 'project version: v0.16.0'
        assert await connection.request('core.millis', 'never', timeout=0.05) is None

    asyncio.run(main())
    assert lizard.stored == SCRIPT


def test_async_connection_keeps_lines_fed_in_one_go():
    lizard = FakeLizard({'project name': 'lizard', 'version': 'v0.15.1'})

    async def main() -> None:
        connection: lh.AsyncConnection

        async def write(line: str) -> None:
            for answer in lizard.handle(lh.split_checksum(line)[0]):
                connection.feed(answer)  # like a read loop that passes on all lines it has read before it yields

        connection = lh.AsyncConnection(write)
        assert await connection.read_version() == lh.LizardVersion.parse('v0.15.1')
        with pytest.raises(lh.UnsupportedFirmware, match='0.16.0'):
            await connection.write_startup('core.telemetry(core.millis)\n')

    asyncio.run(main())
    assert lizard.stored == ''


def test_async_connection_drops_lines_from_before_a_command():
    answers = {'first': ['answer 1', 'answer 2'], 'second': ['answer 3']}

    def two_requests() -> Procedure[List[Optional[str]]]:
        first = yield lh.Exchange('first', lambda line: line.startswith('answer'), 0.05)
        second = yield lh.Exchange('second', lambda line: line.startswith('answer'), 0.05)
        return [first, second]

    async def main() -> None:
        connection: lh.AsyncConnection

        async def write(line: str) -> None:
            for answer in answers[lh.split_checksum(line)[0]]:
                connection.feed(answer)

        connection = lh.AsyncConnection(write)
        assert await connection.run(two_requests()) == ['answer 1', 'answer 3']

    asyncio.run(main())


def test_ready_timeout_grows_with_expanders():
    def ready_timeout(script: str, **kwargs) -> float:
        lizard = FakeLizard()
        procedure = procedures.write_startup(script, **kwargs)
        exchange = next(procedure)
        while True:
            if exchange.command == 'core.restart()' and exchange.expect is not None:
                return exchange.timeout
            answers = lizard.handle(exchange.command) if exchange.command else []
            match = next((line for line in answers if exchange.expect and exchange.expect(line)), None)
            exchange = procedure.send(match)

    assert ready_timeout(SCRIPT) == 15.0
    assert ready_timeout('p0 = Expander(serial)\np1 = Expander(serial2)\n# Expander(x)\n') == 25.0
    assert ready_timeout(SCRIPT, ready_timeout=3.0) == 3.0
