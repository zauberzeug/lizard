#!/usr/bin/env python3
"""Decoder for Lizard telemetry lines, frame format version v1.

A frame is the console line '~' + base64(body) + '@xx' with body = frame id (u8) | seq (u8) | millis (u32) | payload |
CRC-16/CCITT-FALSE over everything before it, all little-endian. Its layout comes as one line per field,
'__LAYOUT__v1 <frame>.<index>/<count> <name>:<type>', with type f (float32), i (int32) or ? (bool); <count> is the
number of fields in the frame, so a reader notices a lost last line (older lines without it are read too, and old logs
may contain e for float16).
The payload holds the numeric fields in index order, then all bools as bits in index order, least significant bit first.
"""
from __future__ import annotations

import argparse
import base64
import binascii
import csv
import logging
import re
import struct
import sys
from collections import Counter
from collections.abc import Hashable, Iterable
from contextlib import ExitStack
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Union

FORMAT_VERSION = 'v1'
SIZES = {'f': 4, 'i': 4, 'e': 2}  # bools ('?') are bits after the numeric fields
LAYOUT_LINE = re.compile(r'__LAYOUT__(v[0-9]+)(?: (.*))?')
LAYOUT_FIELD = re.compile(r'([0-9]{1,3})\.([0-9]+)(?:/([0-9]+))? ([^\s:]+):([fie?])')
Value = Union[float, int, bool]

log = logging.getLogger(__name__)


def line_checksum(text: str) -> str:
    """Lizard's line checksum: XOR of the UTF-8 bytes as two lowercase hex digits."""
    return f'{_xor(text):02x}'


def check_line(raw: str | bytes) -> tuple[str, bool | None]:
    """Strip the line end and a trailing '@xx'; return the text and whether the checksum matched (None if absent)."""
    if isinstance(raw, (bytes, bytearray)):
        raw = raw.decode(errors='replace')
    line = raw.strip('\r\n')
    if line[-3:-2] != '@':
        return line, None
    try:
        check = int(line[-2:], 16)
    except ValueError:
        return line, None
    return line[:-3], _xor(line[:-3]) == check


def encode_frame(frame_id: int, seq: int, millis: int, fields: Iterable[tuple[str, Value]]) -> str:
    """Build a frame line without '@xx' from (type, value) pairs in index order; seq and millis wrap."""
    numbers = bytearray()
    bits = []
    for type_, value in fields:
        if type_ == '?':
            bits.append(bool(value))
        elif type_ in SIZES:
            numbers += struct.pack('<' + type_, value)
        else:
            raise ValueError(f'unknown field type {type_!r}')
    flags = bytearray((len(bits) + 7) // 8)
    for i, bit in enumerate(bits):
        flags[i // 8] |= bit << (i % 8)
    body = struct.pack('<BBI', frame_id, seq % 256, millis % 2**32) + numbers + flags
    body += struct.pack('<H', binascii.crc_hqx(body, 0xffff))
    return '~' + base64.b64encode(body).decode()


def format_value(type_: str, value: Value) -> str:
    """Show a value with the digits its type carries: %.7g for float32, %.4g for float16."""
    if type_ == '?':
        return 'true' if value else 'false'
    if type_ == 'i':
        return str(value)
    return ('%.4g' if type_ == 'e' else '%.7g') % value


@dataclass
class Text:
    sender: Hashable
    line: str

    def __str__(self) -> str:
        return self.line


@dataclass
class Layout:
    sender: Hashable
    version: str
    frame: int | None  # frame, index, name and type are None for an unknown version
    index: int | None
    name: str | None
    type: str | None
    line: str
    count: int | None = None  # fields in the frame, None if the line does not say

    def __str__(self) -> str:
        return self.line


@dataclass
class Frame:
    sender: Hashable
    id: int
    seq: int
    millis: int
    status: str  # ok, no_layout, layout_mismatch, layout_incomplete or unknown_version
    values: dict[str, Value] | None  # in index order, None unless ok
    body: bytes
    types: dict[str, str] = field(default_factory=dict)

    @property
    def payload(self) -> bytes:
        return self.body[6:-2]

    def __str__(self) -> str:
        head = f'frame {self.id}{_from(self.sender)} seq={self.seq} millis={self.millis}'
        if self.values is None:
            return f'[{head} {self.status}] {self.payload.hex()}'.rstrip()
        return f'[{head}] ' + ' '.join(f'{name}={format_value(self.types[name], value)}'
                                       for name, value in self.values.items())


@dataclass
class FrameStats:
    frames: int = 0
    seq_gaps: int = 0  # missing sequence numbers
    millis_resets: int = 0  # millis went back by more than a second, i.e. the sender rebooted
    status: Counter[str] = field(default_factory=Counter)
    last_seq: int | None = None
    last_millis: int | None = None


class Decoder:
    """Turn lines into Text, Layout and Frame events, keeping format version, layouts and counters per sender."""

    def __init__(self, strict: bool = True) -> None:
        self.strict = strict  # decode a frame only if its field indices run from 0 without gaps
        self.versions: dict[Hashable, str] = {}
        self.layouts: dict[tuple[Hashable, int], dict[int, tuple[str, str]]] = {}
        self.counts: dict[tuple[Hashable, int], int] = {}  # fields per frame, from layout lines that carry it
        self.stats: dict[tuple[Hashable, int], FrameStats] = {}
        self._warned: set[tuple[Hashable, str]] = set()

    def feed(self, line: str, sender: Hashable = None) -> Text | Layout | Frame:
        """Classify one line given without line end and '@xx' (see check_line)."""
        if line.startswith('~'):
            body = _frame_body(line[1:])
            if body is not None:
                return self._frame(sender, body)
        match = LAYOUT_LINE.fullmatch(line)
        if match:
            layout = self._layout(sender, line, match.group(1), match.group(2) or '')
            if layout is not None:
                return layout
        return Text(sender, line)

    def reset(self, sender: Hashable = None) -> None:
        """Forget everything about a sender: format version, layouts, counters and warnings."""
        self.versions.pop(sender, None)
        self._drop_layouts(sender)
        for key in [key for key in self.stats if key[0] == sender]:
            del self.stats[key]
        self._warned = {key for key in self._warned if key[0] != sender}

    def _layout(self, sender: Hashable, line: str, version: str, rest: str) -> Layout | None:
        if version != FORMAT_VERSION:
            self._set_version(sender, version)
            if (sender, version) not in self._warned:
                self._warned.add((sender, version))
                log.warning('unknown telemetry format version %s%s, ignoring its frames', version, _from(sender))
            return Layout(sender, version, None, None, None, None, line)
        match = LAYOUT_FIELD.fullmatch(rest)
        if match is None or int(match.group(1)) > 255:
            return None
        count = int(match.group(3)) if match.group(3) is not None else None
        if count is not None and not int(match.group(2)) < count <= 256:
            return None
        self._set_version(sender, version)
        frame, index, name, type_ = int(match.group(1)), int(match.group(2)), match.group(4), match.group(5)
        layout = self.layouts.setdefault((sender, frame), {})
        # a new or repeated definition sends its layout from index 0 and may start seq at 0 again, e.g. after
        # core.clear_telemetry() and the same core.telemetry(...)
        redefined = index == 0 or layout.get(index) not in (None, (name, type_))
        if count is not None:
            if self.counts.get((sender, frame)) != count:  # another count: the frame was defined anew
                layout.clear()
                redefined = True
            self.counts[(sender, frame)] = count
        stats = self.stats.get((sender, frame))
        if redefined and stats is not None:  # a new definition starts its sequence numbers anew
            stats.last_seq = stats.last_millis = None
        layout[index] = (name, type_)
        return Layout(sender, version, frame, index, name, type_, line, count)

    def _set_version(self, sender: Hashable, version: str) -> None:
        if self.versions.get(sender) != version:  # layouts of another format version are meaningless
            self.versions[sender] = version
            self._drop_layouts(sender)

    def _drop_layouts(self, sender: Hashable) -> None:
        for key in [key for key in self.layouts if key[0] == sender]:
            del self.layouts[key]
        for key in [key for key in self.counts if key[0] == sender]:
            del self.counts[key]

    def _frame(self, sender: Hashable, body: bytes) -> Frame:
        frame_id, seq, millis = struct.unpack_from('<BBI', body)
        stats = self.stats.setdefault((sender, frame_id), FrameStats())
        stats.frames += 1
        values: dict[str, Value] | None = None
        types: dict[str, str] = {}
        if self.versions.get(sender, FORMAT_VERSION) != FORMAT_VERSION:
            status = 'unknown_version'
        else:
            if stats.last_seq is not None and stats.last_millis is not None:
                if stats.last_millis - millis > 1000:
                    stats.millis_resets += 1
                elif (seq, millis) != (stats.last_seq, stats.last_millis):  # a repeated line is no gap
                    stats.seq_gaps += (seq - stats.last_seq - 1) % 256
            stats.last_seq, stats.last_millis = seq, millis
            status, values, types = self._decode(self.layouts.get((sender, frame_id)), body[6:-2],
                                                 self.counts.get((sender, frame_id)))
        stats.status[status] += 1
        return Frame(sender, frame_id, seq, millis, status, values, body, types)

    def _decode(self, layout: dict[int, tuple[str, str]] | None, payload: bytes,
                count: int | None = None) -> tuple[str, dict[str, Value] | None, dict[str, str]]:
        if not layout:
            return 'no_layout', None, {}
        indices = sorted(layout)
        if count is not None and indices != list(range(count)):  # with the count, a lost last line shows too
            return 'layout_incomplete', None, {}
        if self.strict and indices != list(range(len(indices))):
            return 'layout_incomplete', None, {}
        fields = [layout[index] for index in indices]
        bits_offset = sum(SIZES.get(type_, 0) for _, type_ in fields)
        bools = sum(type_ == '?' for _, type_ in fields)
        if bits_offset + (bools + 7) // 8 != len(payload):
            return 'layout_mismatch', None, {}
        values: dict[str, Value] = {}
        types: dict[str, str] = {}
        offset = bit = 0
        for name, type_ in fields:
            if type_ == '?':
                values[name] = bool((payload[bits_offset + bit // 8] >> (bit % 8)) & 1)
                bit += 1
            else:
                values[name] = struct.unpack_from('<' + type_, payload, offset)[0]
                offset += SIZES[type_]
            types[name] = type_
        return 'ok', values, types


def _xor(text: str) -> int:
    checksum = 0
    for byte in text.encode():
        checksum ^= byte
    return checksum


def _frame_body(text: str) -> bytes | None:
    if len(text) % 4:
        return None
    try:
        body = base64.b64decode(text, validate=True)
    except ValueError:  # includes binascii.Error and non-ASCII input
        return None
    if len(body) < 8 or binascii.crc_hqx(body[:-2], 0xffff) != int.from_bytes(body[-2:], 'little'):
        return None
    return body


def _from(sender: Hashable) -> str:
    return '' if sender is None else f' from {sender}'


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description='Decode Lizard telemetry frames in a console log')
    parser.add_argument('logfile', nargs='?', help='log file to read (default: stdin)')
    parser.add_argument('--csv', metavar='DIR', type=Path, help='write one CSV file per frame id and layout into DIR')
    parser.add_argument('--non-strict', action='store_true', help='also decode frames whose layout has gaps')
    parser.add_argument('--stats', action='store_true', help='print the frame counters to stderr at the end')
    args = parser.parse_args(argv)

    decoder = Decoder(strict=not args.non_strict)
    writers: dict[tuple[int, tuple[str, ...]], Any] = {}
    with ExitStack() as stack:
        stream = stack.enter_context(open(args.logfile, 'rb')) if args.logfile else sys.stdin.buffer
        if args.csv:
            args.csv.mkdir(parents=True, exist_ok=True)
        try:
            for raw in stream:
                text = raw.decode(errors='replace').rstrip('\r\n')
                line, intact = check_line(text)
                event = decoder.feed(line) if intact is not False else None
                if not isinstance(event, Frame):
                    print(text)
                    continue
                print(event)
                if args.csv and event.values is not None:
                    key = (event.id, tuple(event.values))
                    if key not in writers:  # a new layout of the same frame id gets its own file
                        count = sum(frame_id == event.id for frame_id, _ in writers)
                        filename = f'frame_{event.id}.csv' if count == 0 else f'frame_{event.id}_{count + 1}.csv'
                        file = stack.enter_context(open(args.csv / filename, 'w', newline='', encoding='utf-8'))
                        writers[key] = csv.writer(file)
                        writers[key].writerow(['millis', *event.values])
                    writers[key].writerow([event.millis, *(format_value(event.types[name], value)
                                                           for name, value in event.values.items())])
        except KeyboardInterrupt:
            pass
    if args.stats:
        for (sender, frame_id), stats in decoder.stats.items():
            statuses = ', '.join(f'{status} {count}' for status, count in stats.status.items())
            print(f'frame {frame_id}{_from(sender)}: {stats.frames} frames, {stats.seq_gaps} missing seq, '
                  f'{stats.millis_resets} millis resets ({statuses})', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
