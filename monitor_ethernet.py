#!/usr/bin/env python3
"""
Multi-target monitor for Lizard EthernetLink modules.

Usage:
    monitor_ethernet.py [name=]host[:port] [[name=]host[:port] ...]

If port is omitted, 8080 is used.
If name is omitted, the host is used as name.

Examples:
    monitor_ethernet.py 192.168.1.42
    monitor_ethernet.py 192.168.1.42:8080 192.168.1.43:8080
    monitor_ethernet.py esp1=192.168.1.42 esp2=192.168.1.43

Sending:
    > foo.bar()            broadcast to all connected targets
    > @esp1 foo.bar()      send only to esp1
    > @esp1,esp2 foo.bar() send to a subset
"""
import asyncio
import sys
from dataclasses import dataclass

from prompt_toolkit import PromptSession
from prompt_toolkit.patch_stdout import patch_stdout

DEFAULT_PORT = 8080


@dataclass
class Target:
    name: str
    host: str
    port: int
    reader: asyncio.StreamReader | None = None
    writer: asyncio.StreamWriter | None = None


def parse_targets(argv: list[str]) -> list[Target]:
    if not argv:
        raise SystemExit(__doc__)
    targets: list[Target] = []
    for arg in argv:
        if '=' in arg:
            name, hostspec = arg.split('=', 1)
        else:
            name, hostspec = '', arg
        if ':' in hostspec:
            host, port_str = hostspec.rsplit(':', 1)
            port = int(port_str)
        else:
            host, port = hostspec, DEFAULT_PORT
        if not name:
            name = host
        targets.append(Target(name=name, host=host, port=port))
    names = [t.name for t in targets]
    if len(set(names)) != len(names):
        raise SystemExit(f'duplicate target names: {names}')
    return targets


def verify_checksum(line: str) -> str:
    if len(line) >= 3 and line[-3] == '@':
        try:
            expected = int(line[-2:], 16)
        except ValueError:
            return line
        payload = line[:-3]
        checksum = 0
        for c in payload:
            checksum ^= ord(c)
        if checksum != expected:
            print(f'ERROR: CHECKSUM MISMATCH ({checksum} vs. {expected} for "{payload}")')
        return payload
    return line


def encode_with_checksum(line: str) -> bytes:
    checksum = 0
    for c in line:
        checksum ^= ord(c)
    return f'{line}@{checksum:02x}\n'.encode('utf-8')


async def receive_from(target: Target, label_width: int) -> None:
    assert target.reader is not None
    label = f'[{target.name}]'.ljust(label_width)
    while True:
        try:
            raw = await target.reader.readline()
        except (ConnectionError, asyncio.IncompleteReadError) as e:
            print(f'{label} disconnected: {e}')
            return
        if not raw:
            print(f'{label} closed by peer')
            return
        try:
            line = raw.decode('utf-8').rstrip('\r\n')
        except UnicodeDecodeError:
            print(f'{label} ERROR: COULD NOT DECODE LINE')
            continue
        line = verify_checksum(line)
        print(f'{label} {line}')
        await asyncio.sleep(0)


HELP_TEXT = """\
Routing:
  > <command>              broadcast to all connected ESPs
  > @name <command>        send only to the ESP named <name>
  > @n1,n2 <command>       send to a subset
Local commands (not sent to ESPs):
  help                     show this help
  targets                  list connected ESPs
Each incoming line is prefixed with [name] of the ESP it came from.
Press Ctrl-C or Ctrl-D to quit."""


def select_recipients(input_line: str, targets: list[Target]) -> tuple[list[Target], str]:
    if input_line.startswith('@'):
        head, _, rest = input_line.partition(' ')
        wanted = set(head[1:].split(','))
        chosen = [t for t in targets if t.name in wanted]
        unknown = wanted - {t.name for t in chosen}
        if unknown:
            print(f'ERROR: unknown target(s): {",".join(sorted(unknown))}')
            return [], ''
        return chosen, rest
    return targets, input_line


async def send_loop(targets: list[Target]) -> None:
    session = PromptSession()
    while True:
        try:
            with patch_stdout():
                line = await session.prompt_async('> ')
        except (KeyboardInterrupt, EOFError):
            print('Bye!')
            return
        line = line.strip()
        if not line:
            continue
        if line in ('help', '?'):
            print(HELP_TEXT)
            continue
        if line == 'targets':
            print('connected: ' + ', '.join(f'{t.name} ({t.host}:{t.port})' for t in targets))
            continue
        recipients, payload = select_recipients(line, targets)
        if not recipients or not payload:
            continue
        data = encode_with_checksum(payload)
        for t in recipients:
            if t.writer is None:
                continue
            try:
                t.writer.write(data)
                await t.writer.drain()
            except ConnectionError as e:
                print(f'[{t.name}] send failed: {e}')


async def main(argv: list[str]) -> None:
    targets = parse_targets(argv)
    label_width = max(len(t.name) for t in targets) + 2

    connected: list[Target] = []
    for t in targets:
        print(f'Connecting to {t.name} at {t.host}:{t.port} ...')
        try:
            t.reader, t.writer = await asyncio.open_connection(t.host, t.port)
            sock = t.writer.get_extra_info('socket')
            if sock is not None:
                import socket as _s
                sock.setsockopt(_s.IPPROTO_TCP, _s.TCP_NODELAY, 1)
            connected.append(t)
        except OSError as e:
            print(f'[{t.name}] connection failed: {e}')

    if not connected:
        raise SystemExit('no targets connected')

    print(f'Connected to: {", ".join(t.name for t in connected)}')
    print('Type "help" for routing syntax.')

    receivers = [asyncio.create_task(receive_from(t, label_width)) for t in connected]
    sender = asyncio.create_task(send_loop(connected))

    done, pending = await asyncio.wait(
        [sender, *receivers],
        return_when=asyncio.FIRST_COMPLETED,
    )
    for task in pending:
        task.cancel()
    for t in connected:
        if t.writer is not None:
            t.writer.close()


if __name__ == '__main__':
    try:
        asyncio.run(main(sys.argv[1:]))
    except KeyboardInterrupt:
        pass
