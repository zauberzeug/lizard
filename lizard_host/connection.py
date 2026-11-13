"""Run the procedures of ``procedures.py`` on a serial port (``Connection``) or in an async host."""
import asyncio
import logging
import time
from typing import Any, Awaitable, Callable, List, Mapping, Optional, Tuple

from . import procedures
from .console_line import checksum, format_line, split_checksum
from .procedures import CORE, Exchange, Procedure, T, Target
from .version import LizardVersion

log = logging.getLogger('lizard_host')


class Connection:
    """Runs procedures on a serial port, e.g. a ``serial.Serial`` with a read timeout.

    ``on_line`` gets every line that arrives, without line end and checksum, also those a procedure waits for;
    ``on_send`` gets every line that is sent. Lines with a wrong checksum are dropped.
    """

    def __init__(self, port: Any, *,
                 on_line: Optional[Callable[[str], None]] = None,
                 on_send: Optional[Callable[[str], None]] = None) -> None:
        self.port = port
        self.on_line = on_line
        self.on_send = on_send
        self._buffer = bytearray()

    def run(self, procedure: Procedure[T]) -> T:
        try:
            exchange = next(procedure)
            while True:
                exchange = procedure.send(self._exchange(exchange))
        except StopIteration as stop:
            return stop.value

    def _exchange(self, exchange: Exchange) -> Optional[str]:
        if exchange.command is not None:
            self._drain()  # an answer that arrived before the command cannot be the answer to it
            self.write(exchange.command)
        if exchange.expect is None:
            return None
        deadline = time.monotonic() + exchange.timeout
        while time.monotonic() < deadline:
            line = self.read_line()
            if line is not None and exchange.expect(line):
                return line
        return None

    def write(self, text: str) -> None:
        """Send a line, given without line end and checksum."""
        if self.on_send:
            self.on_send(text)
        self.port.write(format_line(text).encode())

    def read_line(self) -> Optional[str]:
        """Return the next line, or None if none arrives within the read timeout of the port."""
        while True:
            line = self._next_buffered_line()
            if line is not None:
                return line
            data = self.port.read(max(1, self.port.in_waiting))
            if not data:
                return None
            self._buffer.extend(data)

    def _drain(self) -> None:
        if self.port.in_waiting:
            self._buffer.extend(self.port.read(self.port.in_waiting))
        while self._next_buffered_line() is not None:
            pass

    def _next_buffered_line(self) -> Optional[str]:
        while True:
            end = self._buffer.find(b'\n')
            if end < 0:
                return None
            raw = self._buffer[:end].decode(errors='replace')  # noise or a baud mismatch must not end the read
            del self._buffer[:end + 1]
            text = _checked(raw)
            if text is None:
                continue
            if self.on_line:
                self.on_line(text)
            return text

    def send(self, command: str, target: Target = CORE) -> None:
        self.run(procedures.send(command, target))

    def request(self, command: str, prefix: str, target: Target = CORE, *, timeout: float = 3.0) -> Optional[str]:
        return self.run(procedures.request(command, prefix, target, timeout=timeout))

    def read_startup_checksum(self, target: Target = CORE, *, timeout: float = 3.0) -> Optional[int]:
        return self.run(procedures.read_startup_checksum(target, timeout=timeout))

    def read_version(self, target: Target = CORE, *, timeout: float = 3.0) -> Optional[LizardVersion]:
        return self.run(procedures.read_version(target, timeout=timeout))

    def write_startup(self, script: str, target: Target = CORE, **kwargs: Any) -> None:
        self.run(procedures.write_startup(script, target, **kwargs))

    def write_startups(self, scripts: Mapping[Target, str], **kwargs: Any) -> None:
        self.run(procedures.write_startups(scripts, **kwargs))


class AsyncConnection:
    """Runs procedures in an async host that reads the lines itself.

    ``write`` sends a line, given with checksum and without line end.
    The host passes every line it reads to ``feed()``, after checking and removing its checksum.
    """

    def __init__(self, write: Callable[[str], Awaitable[None]]) -> None:
        self._write = write
        self._waiters: List[Tuple[Callable[[str], bool], asyncio.Future[str]]] = []

    def feed(self, text: str) -> None:
        """Pass a line to the procedures waiting for it."""
        for expect, future in self._waiters:
            if not future.done() and expect(text):
                future.set_result(text)

    async def run(self, procedure: Procedure[T]) -> T:
        try:
            exchange = next(procedure)
            while True:
                exchange = procedure.send(await self._exchange(exchange))
        except StopIteration as stop:
            return stop.value

    async def _exchange(self, exchange: Exchange) -> Optional[str]:
        if exchange.expect is None:
            if exchange.command is not None:
                await self.write(exchange.command)
            return None
        waiter = (exchange.expect, asyncio.get_running_loop().create_future())
        self._waiters.append(waiter)  # before sending, so that a fast answer is not missed
        try:
            if exchange.command is not None:
                await self.write(exchange.command)
            return await asyncio.wait_for(waiter[1], exchange.timeout)
        except asyncio.TimeoutError:
            return None
        finally:
            self._waiters.remove(waiter)

    async def write(self, text: str) -> None:
        """Send a line, given without line end and checksum."""
        await self._write(format_line(text).rstrip('\n'))

    async def send(self, command: str, target: Target = CORE) -> None:
        await self.run(procedures.send(command, target))

    async def request(self, command: str, prefix: str, target: Target = CORE, *,
                      timeout: float = 3.0) -> Optional[str]:
        return await self.run(procedures.request(command, prefix, target, timeout=timeout))

    async def read_startup_checksum(self, target: Target = CORE, *, timeout: float = 3.0) -> Optional[int]:
        return await self.run(procedures.read_startup_checksum(target, timeout=timeout))

    async def read_version(self, target: Target = CORE, *, timeout: float = 3.0) -> Optional[LizardVersion]:
        return await self.run(procedures.read_version(target, timeout=timeout))

    async def write_startup(self, script: str, target: Target = CORE, **kwargs: Any) -> None:
        await self.run(procedures.write_startup(script, target, **kwargs))

    async def write_startups(self, scripts: Mapping[Target, str], **kwargs: Any) -> None:
        await self.run(procedures.write_startups(scripts, **kwargs))


def _checked(raw: str) -> Optional[str]:
    text, check = split_checksum(raw)
    if check is not None and checksum(text) != check:
        log.debug('Dropping a line with a wrong checksum: %s', raw)
        return None
    return text
